#!/usr/bin/env python3
#
# Copyright (C) 2021-2022 Intel Corporation.
#
# SPDX-License-Identifier: BSD-3-Clause
#
import socket
import sys

class SocketClient:
    def __init__(self):
        pass
    def connect_to_server(self):
        SOKET_ADDR = sys.argv[1]
        ARGUMENTS = sys.argv[2] + '@' + sys.argv[3]
        JACK_CONNECT_REQ = '{\"command\":\"jack_connect\",\"arguments\":\"' + ARGUMENTS + '\"}'
        BUF_LEN = 1024

        print(JACK_CONNECT_REQ)
        # unix domain sockets
        server_address = SOKET_ADDR
        socket_family = socket.AF_UNIX
        socket_type = socket.SOCK_STREAM

        sock = socket.socket(socket_family, socket_type)
        sock.connect(server_address)
        sock.sendall(JACK_CONNECT_REQ.encode())
        data = sock.recv(BUF_LEN)
        print(f"Waiting for ACK message...: {data.decode()}")
        sock.close()

if __name__ == "__main__":
       socket_client_obj = SocketClient()
       socket_client_obj.connect_to_server()
