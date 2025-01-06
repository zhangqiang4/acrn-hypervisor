/*
 * Copyright (C) 2018-2025 Intel Corporation.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/**
 * @defgroup vp-base_vm_config vp-base.vm-config
 * @ingroup vp-base
 * @brief VM config operation implementation
 *
 * This module contains implementation to manage VM configuration related
 * functionalities.
 *
 * @{
 */

/**
 * @file
 * @brief VM configuration related implementation
 *
 * This file contains implementation related to VM configuration operation.
 */

#include <asm/vm_config.h>
#include <util.h>
#include <rtl.h>

/**
 * @brief Get VM config struct associated with \p vm_id.
 *
 * Get ACRN VM config struct associated with \p vm_id.
 *
 * @pre vm_id < CONFIG_MAX_VM_NUM
 * @post N/A
 *
 * @param[in] vm_id VM ID of VM to obtain VM config structure.
 *
 * @return ACRN VM config struct associated with \p vm_id
 */
struct acrn_vm_config *get_vm_config(uint16_t vm_id)
{
	return &vm_configs[vm_id];
}

/**
 * @brief Get VM severity associated with \p vm_id.
 *
 * Get VM severity associated with \p vm_id.
 *
 * @pre vm_id < CONFIG_MAX_VM_NUM
 * @post N/A
 *
 * @param[in] vm_id VM ID of VM to obtain severity.
 *
 * @return Severity value associated with \p vm_id.
 */
uint8_t get_vm_severity(uint16_t vm_id)
{
	return vm_configs[vm_id].severity;
}

/**
 * @brief Check if a VM's name matches \p name
 *
 * @pre vmid < CONFIG_MAX_VM_NUM
 * @post N/A
 *
 * @param[in] vmid VM ID whose name is to be checked
 * @param[in] name Name to be compared against name associated with \p vmid
 *
 * @return true if VM's name is the same as \p name, false otherwise.
 */
bool vm_has_matched_name(uint16_t vmid, const char *name)
{
	struct acrn_vm_config *vm_config = get_vm_config(vmid);

	return (strncmp(vm_config->name, name, MAX_VM_NAME_LEN) == 0);
}

/**
 * @}
 */
