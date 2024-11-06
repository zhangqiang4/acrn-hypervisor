import os
import sys
import argparse
from defusedxml.lxml import parse

import logging
from extractors.helpers import add_child, get_node
from inspectorlib import validator

script_dir = os.path.dirname(os.path.realpath(__file__))
sys.path.append(os.path.join(script_dir))

import cppyy
import inspectorlib.camera as camera
from cppyy.gbl.std import vector


def cameras_init():
    try:
        cppyy.include('ICamera.h')
        cppyy.include("Parameters.h")
        cppyy.load_library('libcamhal.so')
    except Exception as e:
        logging.warning(f"The file cannot be found in the system. {e}")
        return False
    cppyy.gbl.icamera.camera_hal_init()
    return True


def extract_cameras(device_classes_node):
    if not cameras_init():
        add_child(device_classes_node, "camera_list", "\n\t/* Camera data is not available */\n\t")
        return

    camera_number = cppyy.gbl.icamera.get_number_of_cameras()
    cameras_node = add_child(device_classes_node, "camera_list", None)

    stream_list = cppyy.gbl.icamera.stream_array_t()
    for index in range(0, camera_number):
        camera_node = add_child(cameras_node, "camera", None, id=f"{index}")
        stream_list_node = add_child(camera_node, "stream_list", None)

        result = cppyy.gbl.icamera.camera_device_open(index)
        info = cppyy.gbl.icamera.camera_info_t()
        result = cppyy.gbl.icamera.get_camera_info(index, info)

        info.capability.getSupportedStreamConfig(stream_list)
        for i in range(0, stream_list.size()):
            stream_size_node = add_child(stream_list_node, "stream_size", None, id=f"{i}")
            add_child(stream_size_node, "width", f"{stream_list[i].width}")
            add_child(stream_size_node, "height", f"{stream_list[i].height}")
            add_child(stream_size_node, "format", f"{camera.CAMERA_FORMATS_DB[hex(stream_list[i].format)]}")
        cppyy.gbl.icamera.camera_device_close(index)


def extract_topology(device_classes_node, board_etree):
    extract_cameras(device_classes_node)


def extract(args, board_etree):
    device_classes_node = get_node(board_etree, "//device-classes")
    extract_topology(device_classes_node, board_etree)


def main(board_name, board_xml, args):
    print(f"Adding camera configuration to {board_name}.xml.")
    board_etree = parse(board_xml)
    extract(args, board_etree)
    # Finally overwrite the output with the updated XML
    board_etree.write(board_xml, pretty_print=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("board_name", help="the name of the board that runs the ACRN hypervisor")
    parser.add_argument("--out", help="the name of board info file")
    parser.add_argument("--loglevel", default="warning",
                        help="choose log level, e.g. debug, info, warning, error or critical")

    args = parser.parse_args()
    try:
        logger = logging.getLogger()
        logger.setLevel(args.loglevel.upper())
        formatter = logging.Formatter('%(asctime)s-%(name)s-%(levelname)s:-%(message)s', datefmt='%Y-%m-%d %H:%M:%S')

        sh = logging.StreamHandler()
        sh.setLevel(args.loglevel.upper())

        sh.setFormatter(formatter)
        logger.addHandler(sh)

    except ValueError:
        print(f"{args.loglevel} is not a valid log level")
        print(f"Valid log levels (non case-sensitive): critical, error, warning, info, debug")
        sys.exit(1)

    board_xml = args.out if args.out else f"{args.board_name}.xml"
    main(args.board_name, board_xml, args)
