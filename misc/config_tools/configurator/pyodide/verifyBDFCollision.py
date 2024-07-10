#!/usr/bin/env python3
#
# Copyright (C) 2024 Intel Corporation.
#
# SPDX-License-Identifier: BSD-3-Clause
#

__package__ = 'configurator.pyodide'

import re

from lxml import etree

from .pyodide import convert_result, nuc11_board, nuc11_scenario


def validate_(bdf):
    pat = re.compile(r"([0-9a-fA-F]{1,2}:[0-1][0-9A-Fa-f]\.[0-7]).*")
    if (m := re.search(pat, bdf.strip())) and (m1 := m.group(1).strip()):
        return m1
    else:
        return ""


def _valid(scenario_etree, bdfs):
    vuart_vbdf = scenario_etree.xpath('/acrn-config/hv/DEBUG_OPTIONS/VUART_VBDF/text()')[0].strip()
    vuart_vbdf_ = validate_(vuart_vbdf)
    return True if bdfs and vuart_vbdf_ and vuart_vbdf_ not in bdfs else False


def validate_pci(board, scenario):
    if board.startswith('<?xml version'):
        board = '\n'.join(board.split('\n')[1:])
    board_etree = etree.fromstring(board)

    if scenario.startswith('<?xml version'):
        scenario = '\n'.join(scenario.split('\n')[1:])
    scenario_etree = etree.fromstring(scenario)

    pci_devs_total = board_etree.xpath('/acrn-config/PCI_DEVICE/text()')[0].split('\n')
    vbdfs = []
    for pci in pci_devs_total:
        if not pci:
            continue
        if m1 := validate_(pci):
            if m1.startswith('00:00.0'):
                continue
            vbdfs.append(m1)

    return _valid(scenario_etree, vbdfs)


def validate_vuart(scenario):
    vbdfs = []
    if scenario.startswith('<?xml version'):
        scenario = '\n'.join(scenario.split('\n')[1:])
    scenario_etree = etree.fromstring(scenario)
    vuart_vbdf_list = scenario_etree.xpath('/acrn-config/hv/vuart_connections/vuart_connection')
    if not vuart_vbdf_list:
        return
    for vuart in vuart_vbdf_list:
        p = vuart.xpath('endpoint')
        from_, to_ = validate_(p[0].xpath('vbdf/text()')[0].strip()), validate_(p[1].xpath('vbdf/text()')[0].strip())
        if from_ and to_:
            vbdfs.append(from_)
            vbdfs.append(to_)

    return _valid(scenario_etree, vbdfs)


def validate_ivshmem(scenario):
    vbdfs = []
    if scenario.startswith('<?xml version'):
        scenario = '\n'.join(scenario.split('\n')[1:])
    scenario_etree = etree.fromstring(scenario)
    ivshmem_vbdf_list = scenario_etree.xpath('/acrn-config/hv/FEATURES/IVSHMEM')
    if not ivshmem_vbdf_list:
        return
    for ivshmem in ivshmem_vbdf_list:
        p = ivshmem.xpath('//IVSHMEM_VMS/IVSHMEM_VM')
        from_, to_ = validate_(p[0].xpath('VBDF/text()')[0].strip()), validate_(p[1].xpath('VBDF/text()')[0].strip())
        if from_ and to_:
            vbdfs.append(from_)
            vbdfs.append(to_)

    return _valid(scenario_etree, vbdfs)


def validate(board, scenario):
    pci_result = validate_pci(board, scenario)
    vuart_result = validate_vuart(scenario)
    ivshmem_result = validate_ivshmem(scenario)
    result = {
        'pci': pci_result,
        'vuart': vuart_result,
        'ivshmem': ivshmem_result
    }
    if vuart_result is None:
        del result['vuart']
    if ivshmem_result is None:
        del result['ivshmem']

    print(result)
    ret = True
    for k, v in result.items():
        ret = ret and v
    result = {'result': ret}
    return convert_result(result)


main = validate


def test():
    main(nuc11_board, nuc11_scenario)


if __name__ == '__main__':
    test()
