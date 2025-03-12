#!/usr/bin/env python3
#
# Copyright (C) 2025 Intel Corporation.
#
# SPDX-License-Identifier: BSD-3-Clause
#
import logging
import sys, os, re
sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'library'))
import acrn_config_utilities, lib.error, lib.lib
from collections import namedtuple
from acrn_config_utilities import get_node

class CpuFeaturePolicy(namedtuple("CpuFeaturePolicy", ["feature_flag", "host_condition_list", "extra_check_fn", "auto_enable"])):
    pass

def check_hwp(vm_id, board_etree, scenario_etree, allocation_etree):
    vm_node = get_node(f"/acrn-config/vm[@id = '{vm_id}']", scenario_etree)
    load_order = vm_node.find("load_order").text
    if (lib.lib.is_service_vm(load_order) or \
        (get_node(".//own_pcpu/text()", vm_node) == "y" and get_node(".//os_type/text()", vm_node) != "Windows OS")):
            return True
    return False

policies = {
    "vhwp": CpuFeaturePolicy("GUEST_CPU_FEATURE_VHWP", ["hwp_supported"], check_hwp, True),
    "tm": CpuFeaturePolicy("GUEST_CPU_FEATURE_TM_PT", [], None, False),
    "ptm": CpuFeaturePolicy("GUEST_CPU_FEATURE_PTM_PT", [], None, False),
    "lbr": CpuFeaturePolicy("GUEST_CPU_FEATURE_LBR_PT", [], None, False),
}

def check_host_cpu(processor_node, condition_list):
    models = processor_node.xpath("//model")
    for model in models:
        for condition in condition_list:
            if not model.xpath(f"capability[@id='{condition}']"):
                logging.error(f"Processor {model.get('description')} doesn't have capability {condition}")
                return False
    return True

def fn(board_etree, scenario_etree, allocation_etree):
    for vm_node in scenario_etree.xpath("//vm"):
        vm_id = vm_node.get('id')
        allocation_vm_node = get_node(f"/acrn-config/vm[@id = '{vm_id}']", allocation_etree)
        if allocation_vm_node is None:
            allocation_vm_node = acrn_config_utilities.append_node("/acrn-config/vm", None, allocation_etree, id = vm_id)

        # Enable auto-enable features
        for p in policies:
            if (policies[p].auto_enable and
                check_host_cpu(get_node(f"/acrn-config/processors", board_etree), policies[p].host_condition_list) and
                ((policies[p].extra_check_fn is None) or policies[p].extra_check_fn(vm_id, board_etree, scenario_etree, allocation_etree))):
                    acrn_config_utilities.append_node("./cpu_features/feature_flag", policies[p].feature_flag, allocation_vm_node)

        cpu_features_node = get_node(f"/acrn-config/vm[@id = '{vm_id}']/cpu_features", scenario_etree)
        if cpu_features_node is None:
            continue

        for cpu_feature_node in cpu_features_node:
            feature = cpu_feature_node.text
            if not feature in policies:
                raise lib.error.SettingError(f"Unknown CPU feature {feature} in vm {vm_id}")

            if not check_host_cpu(get_node(f"/acrn-config/processors", board_etree), policies[feature].host_condition_list):
                raise lib.error.SettingError(f"Host CPU doesn't support {feature}")

            if policies[feature].extra_check_fn is not None:
                if not policies[feature].extra_check_fn(vm_id, board_etree, scenario_etree, allocation_etree):
                    raise lib.error.ResourceError(f"Extra check failed for {feature}")
            
            acrn_config_utilities.append_node("./cpu_features/feature_flag", policies[feature].feature_flag, allocation_vm_node)
            
