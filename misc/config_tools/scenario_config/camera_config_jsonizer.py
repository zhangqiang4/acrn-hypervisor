#!/usr/bin/env python3
#
# Copyright (C) 2024 Intel Corporation.
#
# SPDX-License-Identifier: BSD-3-Clause
#
import os.path
import re
import sys
import argparse
import logging
import typing
import json
import textwrap
from defusedxml.lxml import parse
from copy import deepcopy

from pathlib import Path

config_tools_dir = Path(__file__).absolute().parent.parent
sys.path.insert(0, str(config_tools_dir))

from board_inspector.inspectorlib.camera import CAMERA_FORMATS_DB

t_content = typing.Union[dict, typing.List[dict]]


class Doc:
    def __init__(self, fd, indent=4):
        self.data = {}
        self.fd = fd
        self.indent = indent

    def content(self, content: t_content):
        self.data.update(content)

    def flush(self):
        with self.fd as out:
            json.dump(self.data, out, indent=self.indent)


class GenerateJson:

    def __init__(self, board_file_name, scenario_file_name, json_file_name) -> None:
        self.board_etree = parse(board_file_name)
        self.scenario_etree = parse(scenario_file_name)
        self.file = open(json_file_name, 'w')
        self.doc = Doc(self.file)

    def write_configuration_json(self):
        self.add_defaults()
        self.add_phy_cameras()
        self.add_service_vm()
        self.add_vms()
        self.flush()
        self.close_file()

    def flush(self):
        self.doc.flush()

    def add_defaults(self):
        defaults = {"camera_manager": {
            "port": 8003,
            "address": "127.0.0.1"
        }}
        self.doc.content(defaults)

    def get_phy_camera_node_list_from_board(self):
        return self.board_etree.xpath("/acrn-config/device-classes/camera_list/camera")

    def get_phy_camera_node_list(self):
        return self.scenario_etree.xpath("/acrn-config/hv/phy_camera/phy_camera_connection")

    def add_phy_cameras(self):
        self.phy_camera = {'phy_camera': []}
        self.phy_cam_list = self.get_phy_camera_node_list_from_board()
        phy_camera_list = self.get_phy_camera_node_list()
        for cam_node in phy_camera_list:
            self.add_each_cam(cam_node)

    def add_each_cam(self, cam_node):
        # read from UI
        phy_cam_id = cam_node.find("id").text
        format_ = cam_node.find("format").text

        format = ""
        vals = list(CAMERA_FORMATS_DB.values())
        if format_ in vals:
            format = format_
        else:
            logging.error("The color format is not supported or the name is illegal.")
            return 2

        sensor_name = cam_node.find("sensor_name").text
        devnode = cam_node.find("devnode").text
        native_driver = cam_node.find("native_driver").text

        width, height = cam_node.find("resolution").text.split('*')
        cams_by_vm = {}
        vm_node_list = self.get_vm_node_list()
        for vm_node in vm_node_list:
            load_order = vm_node.find("load_order").text
            vm_name = vm_node.find("name").text
            cams_by_vm[vm_name] = []
            if load_order == 'POST_LAUNCHED_VM':
                phy_cam_ids = vm_node.xpath("virtio_devices/camera/virtual_camera/physical_camera_id/text()")
                cams_by_vm[vm_name].extend(phy_cam_ids)
        share = []
        for k, v in cams_by_vm.items():
            for e in v:
                if phy_cam_id != e:
                    continue
                else:
                    share.append(k)

        driver_base_name = native_driver.split('.')[0]
        phy_cam = {
            'type': {
                'shared': 'PROXY_INTERFACE',
                'owned': 'HAL_INTERFACE'
            },
            'driver': {
                'shared': "libcamera_client.so",
                'owned': f"{driver_base_name}.so"
            }
        }
        phy_cam_mode = 'shared' if len(share) > 1 else 'owned'
        phy_driver_type = phy_cam['type'][phy_cam_mode]
        phy_driver = phy_cam['driver'][phy_cam_mode]

        cam_tmp = {"camera": {}}
        cam_tmp['camera'].update({
            'id': int(phy_cam_id),
            'width': int(width),
            'height': int(height),
            'format': format,
            'sensor_name': sensor_name,
            'driver_type': phy_driver_type,
            'driver': phy_driver,
            'devnode': devnode,
            'native_driver': native_driver,
            'share': share if share else [""]
        })
        self.phy_camera['phy_camera'].append(cam_tmp)
        self.phy_ids = []
        for phy in self.phy_camera['phy_camera']:
            self.phy_ids.append(phy['camera']['id'])
        self.doc.content(self.phy_camera)

    def get_vm_node_list(self):
        return self.scenario_etree.xpath("/acrn-config/vm")

    def add_service_vm(self):
        configs = {"SERVICE_VM": []}
        cam_tmp = {"camera": {}}
        phy_camera_list = self.get_phy_camera_node_list()
        for id_, cam_node in enumerate(phy_camera_list):
            phy_id_ = cam_node.find("id").text
            phy_id = int(phy_id_) if phy_id_ else ""
            cam_tmp['camera'].update({
                'id': id_,
                'phy_id': phy_id
            })
            configs["SERVICE_VM"].append(deepcopy(cam_tmp))
        self.doc.content(configs)

    def add_vms(self):
        vm_node_list = self.get_vm_node_list()
        for vm_node in vm_node_list:
            self.add_each_vm(vm_node)

    def add_each_vm(self, vm_node):
        load_order = vm_node.find("load_order").text
        vm_name = vm_node.find("name").text
        if load_order == 'POST_LAUNCHED_VM':
            configs = {vm_name: []}
            virtio_devs = vm_node.xpath("virtio_devices")
            cam_tmp = {"camera": {}}
            # has camera or not
            cam = []
            for virtio_dev in virtio_devs:
                if cam_ := virtio_dev.xpath("camera"):
                    cam = cam_

            if cam:
                cams = cam[0].xpath("virtual_camera")
                for id_, c in enumerate(cams):
                    phy_id = int(c.xpath("physical_camera_id/text()")[0])
                    if phy_id not in self.phy_ids:
                        continue
                    cam_tmp['camera'].update({
                        'id': id_,
                        'phy_id': phy_id
                    })
                    configs[vm_name].append(deepcopy(cam_tmp))
                self.doc.content(configs)

    def close_file(self):
        self.file.close()


def main(board_xml, scenario_xml, out_dir):
    try:
        os.mkdir(out_dir)
    except FileExistsError:
        if os.path.isfile(out_dir):
            logging.error(f"Cannot create output directory {out_dir}: File exists")
            return 1
    except Exception as e:
        logging.error(f"Cannot create output directory: {e}")
        return 1

    GenerateJson(board_file_name=board_xml, scenario_file_name=scenario_xml,
                 json_file_name=os.path.join(out_dir, "virtual_camera.json")).write_configuration_json()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--board",
                        help="Specifies the board config XML path and name used to generate the json file")
    parser.add_argument("--scenario",
                        help="Specifies the scenario XML path and name used to generate the json file")
    parser.add_argument("--out", default="output",
                        help="Specifies the directory of the output json file that configure the camera")
    args = parser.parse_args()

    logging.basicConfig(level="INFO")
    sys.exit(main(args.board, args.scenario, args.out))
