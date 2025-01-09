#!/usr/bin/env python3
#
# Copyright (C) 2022 Intel Corporation.
#
# SPDX-License-Identifier: BSD-3-Clause
#

import sys, os
sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'library'))
import acrn_config_utilities, board_cfg_lib, scenario_cfg_lib
from acrn_config_utilities import get_node

HV_RAM_SIZE_MAX = 0x40000000

MEM_ALIGN = 2 * acrn_config_utilities.SIZE_M

def fn(board_etree, scenario_etree, allocation_etree):
    # this dictonary mapped with 'address start':'mem range'
    ram_range = {}

    max_vm_num = int(get_node(f"//hv/CAPACITIES/MAX_VM_NUM/text()", scenario_etree))
    hv_ram_size = acrn_config_utilities.HV_BASE_RAM_SIZE + acrn_config_utilities.VM_RAM_SIZE * max_vm_num
    assert(hv_ram_size <= HV_RAM_SIZE_MAX)
    # reserve 4M memory for ramoops
    ramoops_size = 0x400000
    # reserve 64k per CPU memory for hvlog
    cpu_num = len(board_cfg_lib.get_processor_info())
    hvlog_size = 0x10000 * cpu_num
    # We recommend to put hv ram start address high than 0x2000000 to
    # reduce memory conflict with GRUB/SOS Kernel.
    hv_start_offset = 0x2000000
    total_size = hv_ram_size
    for start_addr in list(board_cfg_lib.USED_RAM_RANGE):
        if hv_start_offset <= start_addr < 0x80000000:
            del board_cfg_lib.USED_RAM_RANGE[start_addr]
    ram_range = board_cfg_lib.get_ram_range()

    avl_start_addr = board_cfg_lib.find_avl_memory(ram_range, str(total_size), hv_start_offset)
    avl_start_addr = acrn_config_utilities.round_up(int(avl_start_addr, 16), MEM_ALIGN)
    hv_reserved_size = acrn_config_utilities.round_up(ramoops_size + hvlog_size, MEM_ALIGN)
    hv_start_addr = avl_start_addr + hv_reserved_size
    board_cfg_lib.USED_RAM_RANGE[hv_start_addr] = hv_ram_size
    acrn_config_utilities.append_node("/acrn-config/hv/MEMORY/HV_RAM_START", hex(hv_start_addr), allocation_etree)
    acrn_config_utilities.append_node("/acrn-config/hv/MEMORY/HV_RAM_SIZE", hex(hv_ram_size), allocation_etree)
    acrn_config_utilities.append_node("/acrn-config/hv/MEMORY/HV_RESERVED_START", hex(avl_start_addr), allocation_etree)
    acrn_config_utilities.append_node("/acrn-config/hv/MEMORY/HV_RESERVED_SIZE", hex(hv_reserved_size), allocation_etree)
    acrn_config_utilities.append_node("/acrn-config/hv/MEMORY/RAMOOPS_START", hex(avl_start_addr), allocation_etree)
    acrn_config_utilities.append_node("/acrn-config/hv/MEMORY/RAMOOPS_SIZE", hex(ramoops_size), allocation_etree)
    acrn_config_utilities.append_node("/acrn-config/hv/MEMORY/HVLOG_START", hex(avl_start_addr + ramoops_size), allocation_etree)
    acrn_config_utilities.append_node("/acrn-config/hv/MEMORY/HVLOG_SIZE", hex(hvlog_size), allocation_etree)
