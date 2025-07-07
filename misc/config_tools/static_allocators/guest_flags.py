#!/usr/bin/env python3
#
# Copyright (C) 2022 Intel Corporation.
#
# SPDX-License-Identifier: BSD-3-Clause
#

import sys, os, re
sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'library'))
import acrn_config_utilities, lib.error, lib.lib
from collections import namedtuple
from acrn_config_utilities import get_node

class GuestFlagPolicy(namedtuple("GuestFlagPolycy", ["condition", "guest_flag"])):
    pass

policies = [
    GuestFlagPolicy(".//locked_in_xapic = 'y'", "GUEST_FLAG_LOCKED_IN_XAPIC"),
    GuestFlagPolicy(".//lapic_passthrough = 'y'", "GUEST_FLAG_LAPIC_PASSTHROUGH"),
    GuestFlagPolicy(".//io_completion_polling = 'y'", "GUEST_FLAG_IO_COMPLETION_POLLING"),
    GuestFlagPolicy(".//secure_world_support = 'y'", "GUEST_FLAG_SECURE_WORLD_ENABLED"),
    GuestFlagPolicy(".//security_vm = 'y'", "GUEST_FLAG_SECURITY_VM"),
    GuestFlagPolicy(".//vm_type = 'RTVM'", "GUEST_FLAG_RT"),
    GuestFlagPolicy(".//vm_type = 'RTVM' and .//load_order = 'PRE_LAUNCHED_VM' and //hv/BUILD_TYPE= 'debug'", "GUEST_FLAG_PMU_PASSTHROUGH"),
    GuestFlagPolicy(".//vm_type = 'TEE_VM'", "GUEST_FLAG_TEE"),
    GuestFlagPolicy(".//vm_type = 'REE_VM'", "GUEST_FLAG_REE"),
]

def fn(board_etree, scenario_etree, allocation_etree):
    for vm_node in scenario_etree.xpath("//vm"):
        vm_id = vm_node.get('id')
        allocation_vm_node = get_node(f"/acrn-config/vm[@id = '{vm_id}']", allocation_etree)
        if allocation_vm_node is None:
            allocation_vm_node = acrn_config_utilities.append_node("/acrn-config/vm", None, allocation_etree, id = vm_id)

        # - When GUEST_FLAG_LOCKED_IN_XAPIC is set, the VM is restricted to xAPIC mode.
        #   ACRN does not support transitioning a guest from xAPIC to x2APIC.
        #   Therefore, GUEST_FLAG_LAPIC_PASSTHROUGH must NOT be set in this case, as it is only applicable
        #   when the VM operates in x2APIC mode.
        # - When GUEST_FLAG_LOCKED_IN_XAPIC is not set, the VM is locked in x2APIC.
        #   GUEST_FLAG_LAPIC_PASSTHROUGH can be set in this case.
        locked_in_xapic_val = vm_node.findtext("locked_in_xapic", default="n")
        lapic_passthrough_val = vm_node.findtext("lapic_passthrough", default="n")
        if locked_in_xapic_val == "y" and lapic_passthrough_val == "y":
            raise ValueError(
                f"[Error] VM(id={vm_id}): 'locked_in_xapic' and 'lapic_passthrough' "
                f"cannot both be set to 'y' at the same time."
            )

        for policy in policies:
            if vm_node.xpath(policy.condition):
                acrn_config_utilities.append_node("./guest_flags/guest_flag", str(policy.guest_flag), allocation_vm_node)
        acrn_config_utilities.append_node("./guest_flags/guest_flag",'GUEST_FLAG_STATIC_VM', allocation_vm_node)
