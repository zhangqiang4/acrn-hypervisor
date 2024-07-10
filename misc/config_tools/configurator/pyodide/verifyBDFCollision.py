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


def validate_load_order(base, name, vbdf):
    vms = base.xpath('vm')
    for vm in vms:
        vm_name = vm.xpath('name/text()')[0].strip()
        load_order = vm.xpath('load_order/text()')[0].strip()
        if vm_name == name and load_order != 'POST_LAUNCHED_VM':
            return True
        elif vm_name == name and load_order == 'POST_LAUNCHED_VM':
            check_ = lambda x: True if (t := int('0x' + x[3:5], 16)) >= 3 and t < 31 else False  # 3,1e
            return check_(vbdf)


def validate_(bdf):
    pat = re.compile(r"([0-9a-fA-F]{1,2}:[0-1][0-9A-Fa-f]\.[0-7]).*")
    if (m := re.search(pat, bdf.strip())) and (m1 := m.group(1).strip()):
        return m1
    else:
        return ""


class VBDFValidator:
    def __init__(self, board, scenario):
        if board.startswith('<?xml version'):
            board = '\n'.join(board.split('\n')[1:])
        self.board = etree.fromstring(board)
        if scenario.startswith('<?xml version'):
            scenario = '\n'.join(scenario.split('\n')[1:])
        self.scenario = etree.fromstring(scenario)

        self.native_ = []
        self.vuart_vbdfs = []
        self.ivshmem_vbdfs = []

    @property
    def scenario_root(self):
        return self.scenario.xpath('/acrn-config')[0]

    @property
    def native(self):
        pci_devs_total = _board[0].split('\n') if (
            _board := self.board.xpath('/acrn-config/PCI_DEVICE/text()')) else []
        for pci in pci_devs_total:
            if not pci:
                continue
            if m1 := validate_(pci):
                if m1.startswith('00:00.0'):
                    continue
                self.native_.append(m1)
        return self.native_

    @property
    def vuart_vbdf(self):
        _scenario = self.scenario_root.xpath('hv/DEBUG_OPTIONS/VUART_VBDF/text()')
        vuart_vbdf_validated = ''
        if _scenario:
            vuart_vbdf = _scenario[0].strip()
            vuart_vbdf_validated = validate_(vuart_vbdf)
        bdfs = [vuart_vbdf_validated] if _scenario else []
        return bdfs

    def validate_native(self, bdfs):
        return not set(bdfs) & set(self.native)

    def valid_vuart_vbdf(self, bdfs):
        return not set(bdfs) & set(self.vuart_vbdf)

    def validate_pci(self):
        return self.valid_vuart_vbdf(self.native)

    def validate_vuart(self):
        vuart_connections_list = self.scenario_root.xpath('hv/vuart_connections/vuart_connection')  # ***
        if not vuart_connections_list:
            return
        vm_cfg = {}
        for conn in vuart_connections_list:
            endpoints = conn.xpath('endpoint')
            for p in endpoints:
                vm_name = p.xpath('vm_name/text()')[0].strip()
                vm_vbdf = validate_(p.xpath('vbdf/text()')[0].strip())

                if not validate_load_order(self.scenario_root, vm_name, vm_vbdf):
                    return False
                else:
                    if not vm_cfg.get(vm_name):
                        vm_cfg[vm_name] = [vm_vbdf]
                    elif vm_vbdf in vm_cfg[vm_name]:
                        return False
                    else:
                        vm_cfg[vm_name].append(vm_vbdf)

        for _, v in vm_cfg.items():
            self.vuart_vbdfs.extend(v)
        vbdfs = list(set(self.vuart_vbdfs))
        if not self.validate_native(vbdfs):
            return False
        else:
            return self.valid_vuart_vbdf(vbdfs)

    def validate_ivshmem(self):
        ivshmem_vbdf_list = self.scenario_root.xpath('hv/FEATURES/IVSHMEM/IVSHMEM_REGION')
        if not ivshmem_vbdf_list:
            return
        vm_cfg = {}
        for ivshmem in ivshmem_vbdf_list:
            ivs = ivshmem.xpath('IVSHMEM_VMS/IVSHMEM_VM')  # ***
            for iv in ivs:
                vm_name = iv.xpath('VM_NAME/text()')[0].strip()  # ***
                vm_vbdf = validate_(iv.xpath('VBDF/text()')[0].strip())

                if not validate_load_order(self.scenario_root, vm_name, vm_vbdf):
                    return False
                else:
                    if not vm_cfg.get(vm_name):
                        vm_cfg[vm_name] = [vm_vbdf]
                    elif vm_vbdf in vm_cfg[vm_name]:
                        return False
                    else:
                        vm_cfg[vm_name].append(vm_vbdf)

        for _, v in vm_cfg.items():
            self.ivshmem_vbdfs.extend(v)
        vbdfs = list(set(self.ivshmem_vbdfs))
        if not self.validate_native(vbdfs):
            return False
        else:
            return self.valid_vuart_vbdf(vbdfs)

    def validate(self):
        pci_result = self.validate_pci()
        vuart_result = self.validate_vuart()
        ivshmem_result = self.validate_ivshmem()
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


def _main(board, scenario):
    return VBDFValidator(board, scenario).validate()


main = _main


def test():
    main(nuc11_board, nuc11_scenario)


if __name__ == '__main__':
    test()
