/*
 * Copyright (C) 2018-2022 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "vmmapi.h"
#include "acpi.h"

static inline int get_vcpu_pm_info(struct vmctx *ctx, int vcpu_id,
				uint64_t pm_type, uint64_t *pm_info)
{
	*pm_info = ((ctx->vmid << PMCMD_VMID_SHIFT) & PMCMD_VMID_MASK)
			| ((vcpu_id << PMCMD_VCPUID_SHIFT) & PMCMD_VCPUID_MASK)
			| (pm_type & PMCMD_TYPE_MASK);

	return vm_get_cpu_state(ctx, pm_info);
}

static inline int get_vcpu_px_cnt(struct vmctx *ctx, int vcpu_id, uint8_t *px_cnt)
{
	uint64_t px_cnt_u64;
	int ret;

	ret = get_vcpu_pm_info(ctx, vcpu_id, ACRN_PMCMD_GET_PX_CNT, &px_cnt_u64);
	*px_cnt = (uint8_t)px_cnt_u64;

	return ret;
}

static int get_vcpu_px_data(struct vmctx *ctx, int vcpu_id,
			int px_num, struct acrn_pstate_data *vcpu_px_data)
{
	uint64_t *pm_ioctl_buf;
	enum acrn_pm_cmd_type cmd_type = ACRN_PMCMD_GET_PX_DATA;

	pm_ioctl_buf = malloc(sizeof(struct acrn_pstate_data));
	if (!pm_ioctl_buf) {
		return -1;
	}

	*pm_ioctl_buf = ((ctx->vmid << PMCMD_VMID_SHIFT) & PMCMD_VMID_MASK)
		| ((vcpu_id << PMCMD_VCPUID_SHIFT) & PMCMD_VCPUID_MASK)
		| ((px_num << PMCMD_STATE_NUM_SHIFT) & PMCMD_STATE_NUM_MASK)
		| cmd_type;

	/* get and validate px data */
	if (vm_get_cpu_state(ctx, pm_ioctl_buf)) {
		free(pm_ioctl_buf);
		return -1;
	}

	memcpy(vcpu_px_data, pm_ioctl_buf,
			sizeof(struct acrn_pstate_data));

	free(pm_ioctl_buf);
	return 0;
}

/* _PPC: Performance Present Capabilities
 * hard code _PPC to 0,  all states are available.
 */
static void dsdt_write_ppc(void)
{
	dsdt_line("    Name (_PPC, Zero)");
}

/* _PCT: Performance Control
 * Both Performance Control and Status Register are set to FFixedHW
 */
static void dsdt_write_pct(void)
{
	dsdt_line("        Method (_PCT, 0, NotSerialized)");
	dsdt_line("        {");
	dsdt_line("            Return (Package (0x02)");
	dsdt_line("            {");
	dsdt_line("                ResourceTemplate ()");
	dsdt_line("                {");
	dsdt_line("                    Register (FFixedHW,");
	dsdt_line("                        0x00,");
	dsdt_line("                        0x00,");
	dsdt_line("                        0x0000000000000000,");
	dsdt_line("                        ,)");
	dsdt_line("                },");
	dsdt_line("");
	dsdt_line("                ResourceTemplate ()");
	dsdt_line("                {");
	dsdt_line("                    Register (FFixedHW,");
	dsdt_line("                        0x00,");
	dsdt_line("                        0x00,");
	dsdt_line("                        0x0000000000000000,");
	dsdt_line("                        ,)");
	dsdt_line("                }");
	dsdt_line("            })");
	dsdt_line("        }");

}

/* _PSS: Performance Supported States
 */
static int dsdt_write_pss(struct vmctx *ctx, int vcpu_id)
{
	uint8_t vcpu_px_cnt;
	int i;
	struct acrn_pstate_data *vcpu_px_data;
	int ret;

	ret = get_vcpu_px_cnt(ctx, vcpu_id, &vcpu_px_cnt);
	/* vcpu_px_cnt = 0 Indicates vcpu supports continuous pstate.
	 * Then we should write _CPC instate of _PSS
	 */
	if (ret || !vcpu_px_cnt) {
		return -1;
	}

	vcpu_px_data = malloc(vcpu_px_cnt * sizeof(struct acrn_pstate_data));
	if (!vcpu_px_data) {
		return -1;
	}

	/* copy and validate px data first */
	for (i = 0; i < vcpu_px_cnt; i++) {
		if (get_vcpu_px_data(ctx, vcpu_id, i, vcpu_px_data + i)) {
			/* something must be wrong, so skip the write. */
			free(vcpu_px_data);
			return -1;
		}
	}

	dsdt_line("");
	dsdt_line("    Method (_PSS, 0, NotSerialized)");
	dsdt_line("    {");
	dsdt_line("      Return (Package (0x%02X)", vcpu_px_cnt);
	dsdt_line("      {");

	for (i = 0; i < vcpu_px_cnt; i++) {

		dsdt_line("          Package (0x%02X)", 6);
		dsdt_line("          {");
		dsdt_line("             0x%08X,",
				(vcpu_px_data + i)->core_frequency);
		dsdt_line("             0x%08X,",
				(vcpu_px_data + i)->power);
		dsdt_line("             0x%08X,",
				(vcpu_px_data + i)->transition_latency);
		dsdt_line("             0x%08X,",
				(vcpu_px_data + i)->bus_master_latency);
		dsdt_line("             0x%08X,",
				(vcpu_px_data + i)->control);
		dsdt_line("             0x%08X",
				(vcpu_px_data + i)->status);

		if (i == (vcpu_px_cnt - 1)) {
			dsdt_line("          }");
		} else {
			dsdt_line("          },");
		}
	}
	dsdt_line("      })");
	dsdt_line("    }");

	free(vcpu_px_data);

	return 0;
}

/* _CPC: Continuous Performance Control
 * Hard code a V3 CPC table, describing HWP register interface.
 */
static void dsdt_write_cpc(void)
{
	dsdt_line("");
	dsdt_line("    Method (_CPC, 0, NotSerialized)");
	dsdt_line("    {");
	dsdt_line("        Return (Package (0x17)");
	dsdt_line("        {");
	dsdt_line("            0x17,");
	dsdt_line("            0x03,");
	dsdt_line("            ResourceTemplate() {Register(FFixedHW, 0x08, 0x00, 0x0000000000000771, 0x04, )},");
	dsdt_line("            ResourceTemplate() {Register(FFixedHW, 0x08, 0x08, 0x00000000000000CE, 0x04, )},");
	dsdt_line("            ResourceTemplate() {Register(FFixedHW, 0x08, 0x10, 0x0000000000000771, 0x04, )},");
	dsdt_line("            ResourceTemplate() {Register(FFixedHW, 0x08, 0x18, 0x0000000000000771, 0x04, )},");
	dsdt_line("            ResourceTemplate() {Register(FFixedHW, 0x08, 0x08, 0x0000000000000771, 0x04, )},");
	dsdt_line("            ResourceTemplate() {Register(FFixedHW, 0x08, 0x10, 0x0000000000000774, 0x04, )},");
	dsdt_line("            ResourceTemplate() {Register(FFixedHW, 0x08, 0x00, 0x0000000000000774, 0x04, )},");
	dsdt_line("            ResourceTemplate() {Register(FFixedHW, 0x08, 0x08, 0x0000000000000774, 0x04, )},");
	dsdt_line("            ResourceTemplate() {Register(SystemMemory, 0x00, 0x00, 0x0000000000000000, , )},");
	dsdt_line("            ResourceTemplate() {Register(SystemMemory, 0x00, 0x00, 0x0000000000000000, , )},");
	dsdt_line("            ResourceTemplate() {Register(SystemMemory, 0x00, 0x00, 0x0000000000000000, , )},");
	dsdt_line("            ResourceTemplate() {Register(FFixedHW, 0x40, 0x00, 0x00000000000000E7, 0x04, )},");
	dsdt_line("            ResourceTemplate() {Register(FFixedHW, 0x40, 0x00, 0x00000000000000E8, 0x04, )},");
	dsdt_line("            ResourceTemplate() {Register(FFixedHW, 0x02, 0x01, 0x0000000000000777, 0x04, )},");
	dsdt_line("            ResourceTemplate() {Register(FFixedHW, 0x01, 0x00, 0x0000000000000770, 0x04, )},");
	dsdt_line("            One,");
	dsdt_line("            ResourceTemplate() {Register(FFixedHW, 0x0A, 0x20, 0x0000000000000774, 0x04, )},");
	dsdt_line("            ResourceTemplate() {Register(FFixedHW, 0x08, 0x18, 0x0000000000000774, 0x04, )},");
	dsdt_line("            Zero,");
	dsdt_line("            Zero,");
	dsdt_line("            Zero");
	dsdt_line("        })");
	dsdt_line("    }");
}

void pm_write_dsdt(struct vmctx *ctx, int ncpu)
{
	int i;
	int ret;
	bool is_cpc = false;
	uint8_t px_cnt;

	/* Scope (_PR) */
	dsdt_line("");
	dsdt_line("  Scope (_SB)");
	dsdt_line("  {");
	for (i = 0; i < ncpu; i++) {
		dsdt_line("    Device (PR%02d)", i);
		dsdt_line("    {");
		dsdt_line("        Name (_HID, \"ACPI0007\")");
		dsdt_line("        Name (_UID, 0x%02X)", i);
		dsdt_line("    }");
	}
	dsdt_line("  }");
	dsdt_line("");

	/* Scope (_PR.CPU(N)) */
	for (i = 0; i < ncpu; i++) {
		dsdt_line("  Scope (_SB.PR%02d)", i);
		dsdt_line("  {");
		dsdt_line("");

		ret = dsdt_write_pss(ctx, i);

		/* if the vm can support px, hv will return the right px cnt.
			then hard code _PPC and _PCT for all vpu */
		if (ret == 0) {
			if (i == 0) {
				dsdt_write_ppc();
				dsdt_write_pct();
			} else {
				dsdt_line("    Method (_PPC, 0, NotSerialized)");
				dsdt_line("    {");
				dsdt_line("      Return (^^PR00._PPC)");
				dsdt_line("    }");
				dsdt_line("");
				dsdt_line("    Method (_PCT, 0, NotSerialized)");
				dsdt_line("    {");
				dsdt_line("      Return (^^PR00._PCT)");
				dsdt_line("    }");
				dsdt_line("");
			}
		}

		ret = get_vcpu_px_cnt(ctx, i, &px_cnt);
		if (ret == 0 && px_cnt == 0) {
			/* px_cnt = 0 Indicates vcpu supports continuous pstate.
			 * Then we can write _CPC
			 */
			is_cpc = true;
		}

		if (is_cpc) {
			if (i == 0) {
				dsdt_write_cpc();
			} else {
				dsdt_line("    Method (_CPC, 0, NotSerialized)");
				dsdt_line("    {");
				dsdt_line("      Return (^^PR00._CPC)");
				dsdt_line("    }");
				dsdt_line("");
			}
		}

		dsdt_line("  }");
	}
}

/* _OSC: Operating System Capabilities
 * Currently only support CPPC v2 capability.
 * CPPC v2 capability: revision 2 of the _CPC object.
 * If all vcpus don't support _CPC object, no need to add _OSC in DSDT.
 */
void osc_write_ospm_dsdt(struct vmctx *ctx, int ncpu)
{
	int ret;
	bool support_cpc = false;
	uint8_t px_cnt;

	/* check px_cnt on vBSP */
	ret = get_vcpu_px_cnt(ctx, 0, &px_cnt);
	if (ret == 0 && px_cnt == 0) {
		/* px_cnt = 0 Indicates vcpu supports continuous pstate.
		 */
		support_cpc = true;
	}
	if (support_cpc) {
		/* Scope (_SB._OSC) */
		dsdt_line("");
		dsdt_line("  Scope (_SB)");
		dsdt_line("  {");
		dsdt_line("    Method (_OSC, 4, NotSerialized) // _OSC: Operating System Capabilities");
		dsdt_line("    {");
		dsdt_line("      CreateDWordField (Arg3, 0x00, STS0)");
		dsdt_line("      CreateDWordField (Arg3, 0x04, CAP0)");
		dsdt_line("      If ((Arg0 == ToUUID (\"0811b06e-4a27-44f9-8d60-3cbbc22e7b48\") /* Platform-wide OSPM Capabilities */))");
		dsdt_line("      {");
		dsdt_line("        If ((Arg1 == One))");
		dsdt_line("        {");
		dsdt_line("          If ((CAP0 & 0x40))");
		dsdt_line("          {");
		dsdt_line("            CAP0 &= 0x00000040");
		dsdt_line("          }");
		dsdt_line("          Else");
		dsdt_line("          {");
		dsdt_line("            STS0 &= 0xFFFFFF00");
		dsdt_line("            STS0 |= 0x02");
		dsdt_line("          }");
		dsdt_line("        }");
		dsdt_line("        Else");
		dsdt_line("        {");
		dsdt_line("          STS0 &= 0xFFFFFF00");
		dsdt_line("          STS0 |= 0x0A");
		dsdt_line("        }");
		dsdt_line("      }");
		dsdt_line("      Else");
		dsdt_line("      {");
		dsdt_line("        STS0 &= 0xFFFFFF00");
		dsdt_line("        STS0 |= 0x06");
		dsdt_line("      }");
		dsdt_line("      Return (Arg3)");
		dsdt_line("    }");
		dsdt_line("  }");
		dsdt_line("");
	}
}
