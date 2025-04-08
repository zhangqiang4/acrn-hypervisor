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

		if (i == 0) {
			dsdt_write_ppc();
			dsdt_write_pct();
			dsdt_write_cpc();
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
			dsdt_line("    Method (_CPC, 0, NotSerialized)");
			dsdt_line("    {");
			dsdt_line("      Return (^^PR00._CPC)");
			dsdt_line("    }");
			dsdt_line("");
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
