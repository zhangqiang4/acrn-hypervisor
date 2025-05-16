/*
 * Copyright (C) 2019-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include <types.h>
#include <asm/vm_config.h>
#include <asm/guest/vm.h>
#include <vacpi.h>
#include <logmsg.h>
#include <util.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/guest/ept.h>
#include <common/dmi.h>
#include <acrn_common.h>
#include <boot.h>

static void *get_acpi_mod_entry(const char *signature, void *acpi)
{
	struct acpi_table_xsdt *xsdt;
	uint32_t i, entry_cnt = 0U;
	struct acpi_table_header *header = NULL, *find = NULL;

	xsdt = acpi + VIRT_XSDT_ADDR - VIRT_ACPI_DATA_ADDR;
	entry_cnt = (xsdt->header.length - sizeof(xsdt->header)) / (sizeof(xsdt->table_offset_entry[0]));

	for (i = 0; i < entry_cnt; i++) {
		header = acpi + xsdt->table_offset_entry[i] - VIRT_ACPI_DATA_ADDR;
		if (strncmp(header->signature, signature, ACPI_NAME_SIZE) == 0) {
			find = header;
			break;
		}
	}

	return find;
}

static void tpm2_fixup(uint16_t vm_id)
{
	struct acpi_table_tpm2 *vtpm2 = NULL, *tpm2 = NULL;
	struct acrn_vm_config *config = get_vm_config(vm_id);
	uint8_t checksum, i;
	struct acrn_boot_info *abi = get_acrn_boot_info();
	struct acrn_mmiodev *dev = NULL;

	const struct abi_module *mod = get_mod_by_tag(abi, config->acpi_config.acpi_mod_tag);
	if (mod != NULL) {
		vtpm2 = get_acpi_mod_entry(ACPI_SIG_TPM2, mod->start);
		tpm2 = get_acpi_tbl(ACPI_SIG_TPM2);

		if (config->pt_tpm2 && (vtpm2 != NULL) && (tpm2 != NULL)) {
			for (i = 0U; i < MAX_MMIO_DEV_NUM; i++) {
				if (strncmp(config->mmiodevs[i].name, "tpm2", 4) == 0) {
					dev = &config->mmiodevs[i];
					break;
				}
			}

			if (dev != NULL) {
				vtpm2->start_method = tpm2->start_method;
				memcpy_s(&vtpm2->start_method_spec_para, sizeof(tpm2->start_method_spec_para),
					 &tpm2->start_method_spec_para, sizeof(tpm2->start_method_spec_para));

				/* tpm2 has event log */
				if (tpm2->laml != 0U) {
					vtpm2->header.length = tpm2->header.length;
					vtpm2->header.revision = tpm2->header.revision;
					vtpm2->laml = tpm2->laml;
					vtpm2->lasa = dev->res[1].user_vm_pa;

					/* update log buffer length/HPA in vm_config */
					dev->res[1].size = tpm2->laml;
					dev->res[1].host_pa = tpm2->lasa;
				}

				/* update checksum */
				vtpm2->header.checksum = 0;
				checksum = calculate_checksum8(vtpm2, sizeof(struct acpi_table_tpm2));
				vtpm2->header.checksum = checksum;
			} else {
				pr_err("%s, no TPM2 in acrn_vm_config", __func__);
			}
		}
	}
}

void security_vm_fixup(uint16_t vm_id)
{
	struct acrn_vm_config *vm_config = get_vm_config(vm_id);

	if ((vm_config->guest_flags & GUEST_FLAG_SECURITY_VM) != 0UL) {
		stac();
		tpm2_fixup(vm_id);
		clac();
	}
}

void passthrough_smbios(struct acrn_vm *vm, struct acrn_boot_info *abi)
{
    struct smbios_info si;
	struct acrn_vm_config *vm_config = get_vm_config(vm->vm_id);

    if (is_prelaunched_vm(vm) && ((vm_config->guest_flags & GUEST_FLAG_SECURITY_VM) != 0)) {
        memset(&si, 0, sizeof(struct smbios_info));

        if (probe_smbios_table(abi, &si)) {
            if (copy_smbios_to_guest(vm, &si)) {
                pr_err("Failed to copy SMBIOS info to vm%d", vm->vm_id);
            }
        }
    }
}

static void generate_checksum(uint8_t *byte_start, int nbytes, uint8_t *checksum_pos)
{
    *checksum_pos = 0;
    *checksum_pos = -calculate_sum8(byte_start, nbytes);
}

static int copy_smbios_to_guest(struct acrn_vm *vm, struct smbios_info *si)
{
    int ret = 0;
    uint64_t gpa;

    gpa = VIRT_SMBIOS_TABLE_ADDR;
    ret = copy_to_gpa(vm, si->smbios_table, gpa, si->smbios_table_size);
    if (ret == 0) {
        if (strncmp("_SM_", si->smbios_eps.eps2.anchor, 4) == 0) {
            struct smbios2_entry_point *eps2 = &si->smbios_eps.eps2;
            eps2->st_addr = (uint32_t)gpa;
            /* If we wrote generate_checksum(eps->int_anchor, ...), the code scanning tool will
             * emit warnings about array bound overflow. So use offsetof instead.
             */
            generate_checksum((uint8_t *)eps2 + offsetof(struct smbios2_entry_point, int_anchor),
                0xf, &eps2->int_checksum);
            generate_checksum((uint8_t *)eps2, eps2->length, &eps2->checksum);
        } else if (strncmp("_SM3_", si->smbios_eps.eps3.anchor, 5) == 0) {
            struct smbios3_entry_point *eps3 = &si->smbios_eps.eps3;
            eps3->st_addr = (uint32_t)gpa;
            generate_checksum((uint8_t *)eps3, eps3->length, &eps3->checksum);
        }

        gpa = VIRT_SMBIOS_EPS_ADDR;
        ret = copy_to_gpa(vm, &si->smbios_eps, gpa, si->smbios_eps_size);
    }

    return ret;
}
