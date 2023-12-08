#!/usr/bin/env python3

import sys, os
import re

def get_vendor_code(port):
   vendor_file = "/sys/bus/usb/devices/"+port+"/idVendor"
   product_file = "/sys/bus/usb/devices/"+port+"/idProduct"
   vendor = ""
   product = ""
   with open(vendor_file, "r") as f:
       vendor = f.read().strip()
   with open(product_file, "r") as f:
       product = f.read().strip()
   device = vendor + ":" + product
   return device

def get_usb_name(device):
    cmd_out = os.popen('lsusb -d {b}'.format(b=device)).read()
    device_name = cmd_out.split(device)[1].strip()
    return device_name

def get_card_name(usb):
    with open("/proc/asound/cards", 'r') as f_card:
        prev = ""
        for line in f_card:
            if usb in line:
                fields = [x for x in prev.strip().split(' ') if x]
                usb_card = fields[1].replace('[', '').replace(']', '')
                return usb_card
            prev = line.strip()

def update_asound(usb_card):
    with open("/etc/asound.conf", 'r+') as f_conf:
        data = ""
        usb_plug = 0
        for line in f_conf:
            if usb_plug == 0:
                #find usb plugin
                if "usb_dmixer" in line or "usb_dsnooper" in line:
                    usb_plug = 1
            elif usb_plug == 1:
                if "slave" in line:
                    usb_plug = 2
            elif usb_plug == 2:
                fields = [x for x in line.strip().split(' ') if x]
                prev = fields[1].replace('\"', '').replace('\"', '')
                dev = "hw:" + usb_card + ',0'
                line = line.replace(prev, dev)
                usb_plug = 0
            data += line
        with open("/etc/asound.conf", "w") as new:
            new.write(data)  

def update_sound_card(port):
    device = get_vendor_code(port)
    name = get_usb_name(device)
    usb_card = get_card_name(name)
    if usb_card == "":
        return
    update_asound(usb_card)
    
def main(args):
    update_sound_card(args[1])

    

if __name__ == '__main__':

    main(sys.argv)


