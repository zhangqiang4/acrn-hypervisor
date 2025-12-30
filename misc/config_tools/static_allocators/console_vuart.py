#!/usr/bin/env python3
#
# Copyright (C) 2025 Intel Corporation.
#
# SPDX-License-Identifier: BSD-3-Clause
#

"""
Console vUART allocator - extracts native UART IRQ and base address from board ACPI information
for Service VM console vUART configuration.

Architecture principle:
- Service VM should use actual hardware UART configuration from ACPI
- User VMs use standard/virtual UART configuration
- Complex ACPI parsing belongs in Python layer, not XSLT templates
"""

import sys, os
sys.path.append(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'library'))
import acrn_config_utilities
from acrn_config_utilities import get_node

# Standard COM port configurations (fallback for User VMs)
COM_PORT_CONFIG = {
    "COM Port 1": {"base": "0x3F8", "irq": "4"},
    "COM Port 2": {"base": "0x2F8", "irq": "3"},
    "COM Port 3": {"base": "0x3E8", "irq": "4"},
    "COM Port 4": {"base": "0x2E8", "irq": "3"},
}

def get_uart_config_from_acpi(board_etree, base_address):
    """
    Extract UART IRQ from board XML ACPI device information (PNP0501/PNP0500).

    Args:
        board_etree: Board XML etree
        base_address: UART base address (e.g., "0x3F8")

    Returns:
        Dictionary with 'base' and 'irq' keys, or None if not found
    """
    # Normalize base address for comparison
    base_addr_int = int(base_address, 16)

    # Search for PNP0501 (16550A UART) or PNP0500 (standard UART) devices
    uart_devices = board_etree.xpath("//device[@id='PNP0501' or @id='PNP0500']")

    for device in uart_devices:
        # Get I/O port resource
        io_resource = get_node("./resource[@type='io_port']/@min", device)
        if io_resource:
            device_base = int(io_resource, 16)
            if device_base == base_addr_int:
                # Found matching UART device, extract IRQ
                irq_resource = get_node("./resource[@type='irq']/@int", device)
                if irq_resource:
                    return {
                        "base": hex(device_base),
                        "irq": str(irq_resource)
                    }

    return None

def get_com_port_base_address(com_port_name):
    """Get standard base address for a COM port name."""
    config = COM_PORT_CONFIG.get(com_port_name)
    return config["base"] if config else None

def allocate_console_vuart_config(board_etree, scenario_etree, allocation_etree):
    """
    Allocate console vUART configuration for each VM.

    For Service VM:
        - Read actual hardware UART configuration from board ACPI
        - Use native IRQ instead of hardcoded standard values

    For User VMs:
        - Use standard COM port configuration (virtual UARTs)
    """
    vm_nodes = scenario_etree.xpath("//vm")

    for vm_node in vm_nodes:
        vm_id = get_node("./@id", vm_node)
        load_order = get_node("./load_order/text()", vm_node)
        console_vuart = get_node("./console_vuart/text()", vm_node)

        # Skip if no console_vuart
        if not console_vuart or console_vuart == "None":
            continue

        # Skip if not a legacy COM port
        if console_vuart not in COM_PORT_CONFIG:
            continue

        # Get standard configuration as base
        config = COM_PORT_CONFIG[console_vuart].copy()

        # For Service VM, try to get actual hardware configuration from ACPI
        if load_order == "SERVICE_VM":
            base_address = config["base"]
            acpi_config = get_uart_config_from_acpi(board_etree, base_address)

            if acpi_config:
                # Use actual hardware configuration
                config = acpi_config

        # Create allocation node structure
        allocation_vm_node = get_node(f"/acrn-config/vm[@id = '{vm_id}']", allocation_etree)
        if allocation_vm_node is None:
            allocation_vm_node = acrn_config_utilities.append_node("/acrn-config/vm", None, allocation_etree, id=vm_id)

        if get_node("./load_order", allocation_vm_node) is None:
            acrn_config_utilities.append_node("./load_order", load_order, allocation_vm_node)

        # Create console_vuart node with configuration
        console_node = get_node("./console_vuart", allocation_vm_node)
        if console_node is None:
            console_node = acrn_config_utilities.append_node("./console_vuart", None, allocation_vm_node)

        # Add base address and IRQ
        if get_node("./base", console_node) is None:
            acrn_config_utilities.append_node("./base", config["base"], console_node)
        else:
            get_node("./base", console_node).text = config["base"]

        if get_node("./irq", console_node) is None:
            acrn_config_utilities.append_node("./irq", config["irq"], console_node)
        else:
            get_node("./irq", console_node).text = config["irq"]

def fn(board_etree, scenario_etree, allocation_etree):
    """Entry point for the static allocator."""
    allocate_console_vuart_config(board_etree, scenario_etree, allocation_etree)
