#include <types.h>
#include <asm/vm_config.h>
#include <asm/guest/vm.h>
#include <vacpi.h>
#include <logmsg.h>
#include <util.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/guest/ept.h>
#include <acrn_common.h>
#include <boot.h>
#include <rtl.h>
#include <dmi.h>
#include <sprintf.h>

/* The region after the first 64kb is currently not used. On some platforms,
 * there might be an TPM2 event log region starting from VIRT_ACPI_NVS_ADDR + 0xB0000.
 * This table is usually a few KB in size so we have plenty of room here.
 */
#define VIRT_SMBIOS_TABLE_ADDR (VIRT_ACPI_NVS_ADDR + 64 * MEM_1K)
/* We hardcode the eps addr to 0xf1000. The ACPI RSDP addr is hardcoded to 0xf2400
 * so we're safe here. This header is at most 31 bytes.
 */
#define VIRT_SMBIOS_EPS_ADDR    0xf1000UL
#define SMBIOS_EPS_SEARCH_START 0xf0000UL
#define SMBIOS_EPS_SEARCH_END   0xfffffUL

/* For subsequent code, "smbios2" will be specifically refering to 32bit SMBIOS (major version 2),
 * and "smbios3" will refer to 64-bit SMBIOS (major version 3). "smbios" will be a generic
 * reference to SMBIOS structure.
 */

static void *efi_search_guid(EFI_SYSTEM_TABLE *tab, EFI_GUID *guid)
{
	uint64_t i;
	void *pVendortable = NULL;

	if (tab != NULL) {
		for (i = 0; i < tab->NumberOfTableEntries; i++) {
			EFI_CONFIGURATION_TABLE *conf_tab = &tab->ConfigurationTable[i];

			if (uuid_is_equal((uint8_t *)&conf_tab->VendorGuid, (uint8_t *)guid)) {
				pVendortable = hpa2hva((uint64_t)conf_tab->VendorTable);
				break;
			}
		}
	}

	return pVendortable;
}

static inline void get_smbios3_info(struct smbios3_entry_point *eps3, struct smbios_info *si)
{
	si->smbios_eps_size = eps3->length;
	memcpy_s(&si->smbios_eps, si->smbios_eps_size, eps3, si->smbios_eps_size);
	si->smbios_table = hpa2hva(eps3->st_addr);
	si->smbios_table_size = eps3->max_st_size;
}

static inline void get_smbios2_info(struct smbios2_entry_point *eps2, struct smbios_info *si)
{
	si->smbios_eps_size = eps2->length;
	memcpy_s(&si->smbios_eps, si->smbios_eps_size, eps2, si->smbios_eps_size);
	si->smbios_table = hpa2hva(eps2->st_addr);
	si->smbios_table_size = eps2->st_length;
}

static int efi_search_smbios_eps(EFI_SYSTEM_TABLE *efi_system_table, struct smbios_info *si)
{
	void *p = NULL;
	EFI_GUID smbios3_guid = SMBIOS3_TABLE_GUID;
	EFI_GUID smbios2_guid = SMBIOS2_TABLE_GUID;

	/* If both are present, SMBIOS3 takes precedence over SMBIOS */
	stac();
	p = efi_search_guid(efi_system_table, &smbios3_guid);
	if (p != NULL) {
		get_smbios3_info((struct smbios3_entry_point *)p, si);
	} else {
		p = efi_search_guid(efi_system_table, &smbios2_guid);
		if (p != NULL) {
			get_smbios2_info((struct smbios2_entry_point *)p, si);
		}
	}
	clac();

	return (p != NULL);
}

static int is_smbios3_present(uint8_t *p)
{
    return ((strncmp((const char *)p, "_SM3_", 5) == 0) &&
           (calculate_sum8(p, ((struct smbios3_entry_point *)p)->length) == 0));
}

static int is_smbios2_present(uint8_t *p)
{
    return ((strncmp((const char *)p, "_SM_", 4) == 0) &&
           (calculate_sum8(p, ((struct smbios2_entry_point *)p)->length) == 0));
}

static int mem_search_smbios_eps(struct smbios_info *si)
{
    uint8_t *start = (uint8_t *)hpa2hva(SMBIOS_EPS_SEARCH_START);
    uint8_t *end = (uint8_t *)hpa2hva(SMBIOS_EPS_SEARCH_END);
    uint8_t *p;

    /* per SMBIOS spec 3.2.0, 32-bit (SMBIOS) and 64-bit (SMBIOS3) EPS can be found by searching
     * for the anchor string on paragraph (16-byte) boundaries within the physical address
     * 0xf0000-0xfffff.
     */
    stac();
    for (p = start; p < end; p += 16) {
        if (is_smbios3_present(p)) {
            get_smbios3_info((struct smbios3_entry_point *)p, si);
            break;
        } else if (is_smbios2_present(p)) {
            get_smbios2_info((struct smbios2_entry_point *)p, si);
            break;
        } else {
            /* No SMBIOS entry point found at this address - continue searching */
        }
    }
    clac();

    return (p < end);
}

static int probe_smbios_table(struct acrn_boot_info *abi, struct smbios_info *si)
{
    int found = 0;

    if (boot_from_uefi(abi)) {
        /* Get from EFI system table */
        uint64_t efi_system_tab_paddr = ((uint64_t)abi->uefi_info.systab_hi << 32) | ((uint64_t)abi->uefi_info.systab);
        EFI_SYSTEM_TABLE *efi_system_tab = (EFI_SYSTEM_TABLE *)hpa2hva(efi_system_tab_paddr);
        found = efi_search_smbios_eps(efi_system_tab, si);
    } else {
        /* Search eps in 0xf0000-0xfffff */
        found = mem_search_smbios_eps(si);
    }
    /* Note: Multiboot2 spec specifies an SMBIOS tag where the bootloader can pass an "SMBIOS table" to OS.
     * As of today GRUB does not support this feature, and if they were to support it, they will do the same
     * thing we did here, i.e.: either reading from EFI system table or scan 0xf0000~0xfffff. So we will skip
     * trying to get SMBIOS table from Multiboot2 tag.
     */

#ifdef VM0_TPM_EVENTLOG_BASE_ADDR
    if (found && (si->smbios_table_size > (VM0_TPM_EVENTLOG_BASE_ADDR - VIRT_SMBIOS_TABLE_ADDR))) {
        /* Unlikely but we check this anyway */
        pr_err("Error: SMBIOS table too large. Stop copying SMBIOS info to guest.");
        found = 0;
    }
#endif

    return found;
}

static struct smbios_board_info runtime;

struct smbios_board_info* get_runtime_smbios_info(void)
{
    return &runtime;
}

static int32_t dmi_save_string(uint8_t *string_section, char *target, uint8_t formatted_index, uint8_t* end)
{
    /* The string section is a series of null-terminated strings, followed by a double null terminator.
     * The first string is at index 1, the second at index 2, etc. The first byte of the string section
     * is the length of the string, and the second byte is the type.
     */
    uint8_t *current_string = string_section;
    int string_index = 1;
    int32_t ret = 0;

    while (*current_string) {
        if (string_index == formatted_index) {
            (void)strncpy_s(target, DMI_STRLEN, (const char *)current_string, DMI_STRLEN);
            break;
        }
        if ((current_string + strnlen_s((char *)current_string, DMI_STRLEN) + 1) >= end) {
            /* Prevent running out of bounds */
            ret = -EINVAL;
            pr_err("DMI string parsing section out of bounds");
            break;
        }
        current_string += strnlen_s((char *)current_string, DMI_STRLEN) + 1;
        string_index++;
    }

    return ret;
}

static uint8_t* advance_to_next_structure(uint8_t *ptr, uint8_t *end, uint8_t length)
{
    /* skip the formatted section */
    uint8_t *next_ptr = ptr + length;

    /* Reach the double null terminator */
    while ((next_ptr + 1U) < end) {
        /* Found double null terminator */
        if ((next_ptr[0] == 0U) && (next_ptr[1] == 0U)) {
            break;
        }
        next_ptr++;
    }

    /* Skip the double null terminator */
    if ((next_ptr + 2U) <= end) {
        next_ptr += 2U;
    } else {
        /* Prevent running out of bounds */
        pr_err("DMI string parsing section out of bounds");
        next_ptr = NULL;
    }
    return next_ptr;
}

static int32_t parse_runtime_smbios_structures(struct smbios_info *si)
{
    uint8_t *ptr = (uint8_t *)si->smbios_table;
    size_t table_size = si->smbios_table_size;
    uint8_t *end = ptr + table_size;
    int32_t ret = 0;

    while (ptr < end){
        struct smbios_header *header = (struct smbios_header *)ptr;

        uint8_t *formatted = ptr + sizeof(struct smbios_header);

        if ((ptr + header->length) > end) {
            ret = -EINVAL;
            pr_err("Firmware Bug: Corrupted DMI table. DMI structure length exceeds table size");
            break;
        }

        switch (header->type) {
        /* Return this function after saved required information or reach the end structure */
        case PLATFORM_FIRMWARE_INFORMATION_STRUCTURE:
            ret = dmi_save_string(ptr + header->length, (char *)&runtime.bios_version[0],
                 formatted[FIRMWARE_VERSION], end);
            break;

        case PROCESSOR_INFORMATION_STRUCTURE:

            snprintf(runtime.processor_core_count, DMI_STRLEN, "%d", formatted[PROCESSOR_CORE_COUNT]);
            snprintf(runtime.processor_core_enabled, DMI_STRLEN, "%d", formatted[PROCESSOR_CORE_ENABLED]);
            snprintf(runtime.processor_thread_count, DMI_STRLEN, "%d", formatted[PROCESSOR_THREAD_COUNT]);

            ret = dmi_save_string(ptr + header->length, (char *)&runtime.processor_version[0],
                 formatted[PROCESSOR_VERSION], end);

            break;

        case END_STRUCTURE:

            return ret;

        default:
            pr_dbg("Unhandled SMBIOS type %d\n", header->type);
        }

        ptr = advance_to_next_structure(ptr, end, header->length);
        if (ptr == NULL) {
            /* out of bounds */
            ret = -EINVAL;
            break;
        }

    }
    return ret;
}

static uint32_t compare_smbios_info(dmi_checkoption* dmi_checklist, uint64_t check_count)
{
    uint32_t ret = 0U;
    uint64_t i;

    for (i = 0U; i < check_count; i++) {
        dmi_checkoption *dc = &dmi_checklist[i];
        pr_dbg("Comparing %s: buildtime=%s, runtime=%s\n", dc->desc, dc->buildtime, dc->runtime);
        if (strcmp(dc->buildtime, dc->runtime) != 0) {
            ret |= 1U << i;
        }
    }

    return ret;
}

bool is_strict_dmi_check(void) {
	/*
	 * Parse cmdline, decide which policy type to use.
	 * If user specify that "strict_dmi_check=True", then return true; If do not has this option, return false.
	 * In strict boot check strategy, hypervisor will panic immediately if any check fails.
	 * Otherwise, warning message will be printed.
	 */
	struct acrn_boot_info *abi = get_acrn_boot_info();
	const char *cmd_src = abi->cmdline;

	return strstr_s(cmd_src, MAX_BOOTARGS_SIZE, "strict_dmi_check=True", 24U) != NULL;
}

static void warn_on_mismatch(dmi_checkoption *dc)
{
    if ((strnlen_s(dc->buildtime, DMI_STRLEN) == 0U) && (strnlen_s(dc->runtime, DMI_STRLEN) != 0U)) {
        /*
         * board.xml does not have this information but runtime has extracted this.
         * User needs to update board_inspector to get this information. 
         */
        pr_warn("%s info missing: Outdated board_inspector detected.", dc->desc);
    } else {
        pr_warn("%s mismatch.", dc->desc);
        pr_warn("Hypervisor build-time %s is: %s", dc->desc, dc->buildtime);
        pr_warn("Run-time %s is: %s", dc->desc, dc->runtime);
    }
}

void check_board_dmi_info(struct acrn_boot_info *abi)
{
    struct smbios_info si;
    uint32_t result = 0U;
    uint32_t i;
    uint64_t check_count;
    memset(&si, 0, sizeof(struct smbios_info));
    
    if (probe_smbios_table(abi, &si) && (parse_runtime_smbios_structures(&si) == 0)) {
        dmi_checkoption dmi_checklist[] = {
            {"Firmware version", get_smbios_info_from_board()->bios_version,
                 get_runtime_smbios_info()->bios_version},
            {"Processor version", get_smbios_info_from_board()->processor_version,
                 get_runtime_smbios_info()->processor_version},
            {"Processor core count", get_smbios_info_from_board()->processor_core_count,
                 get_runtime_smbios_info()->processor_core_count},
            {"Processor core enabled", get_smbios_info_from_board()->processor_core_enabled,
                 get_runtime_smbios_info()->processor_core_enabled},
            {"Processor thread count", get_smbios_info_from_board()->processor_thread_count,
                 get_runtime_smbios_info()->processor_thread_count}
        };
        check_count = ARRAY_SIZE(dmi_checklist);
        result = compare_smbios_info(dmi_checklist, check_count);

        if (result != 0U) {
            pr_warn("Hypervisor was built with board information different from current running hardware."
                "This might incur potential compatibility issue.");

            for (i = 0U; i < check_count; i++) {
                if (result & (1U << i)) {
                    warn_on_mismatch(&dmi_checklist[i]);
                }
            }

            if (is_strict_dmi_check()) {
                panic("strict_dmi_check turned on. Panicking Hypervisor.");
            } else {
                pr_warn("To enforce strict checking, add \"strict_dmi_check=True\" to ACRN command line.");
            }
        }
    } else {
        pr_warn("SMBIOS table not found or parsing failed. Stop comparing SMBIOS info."
            "This might incur potential compatibility issue.");
    }
}
