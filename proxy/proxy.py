#!/bin/python3

import argparse
import random
import socket
import select
import sys
import threading


class Proxy:
    def __init__(self, args):
        self.args = args
        self.sent_packets = 0
        self.dropped_packets = 0
        self.lock = threading.Lock()

    def start(self):
        if self.args.proto == 'udp':
            self.start_udp()
        else:
            self.start_tcp()

    def start_udp(self):
        sock1 = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock1.bind(('0.0.0.0', self.args.client1_recv))
        sock2 = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock2.bind(('0.0.0.0', self.args.client2_recv))

        print(f"UDP proxy started. Listening on ports {self.args.client1_recv} (Client1) "
              f"and {self.args.client2_recv} (Client2). Forwarding to {
            self.args.client2_listen} "
            f"and {self.args.client1_listen} respectively.")

        try:
            while True:
                rlist, _, _ = select.select([sock1, sock2], [], [])
                for sock in rlist:
                    data, addr = sock.recvfrom(65535)
                    with self.lock:
                        if random.random() < self.args.drop_rate:
                            self.dropped_packets += 1
                            print(f"Dropped packet from {
                                  'Client1' if sock == sock1 else 'Client2'}")
                        else:
                            dest_port = self.args.client2_listen if sock == sock1 else self.args.client1_listen
                            target_sock = sock2 if sock == sock1 else sock1
                            target_sock.sendto(data, ('localhost', dest_port))
                            self.sent_packets += 1
                            print(f"Forwarded {len(data)} bytes from {'Client1' if sock == sock1 else 'Client2'} "
                                  f"to port {dest_port}")

                        print(f"Stats: Sent={self.sent_packets}, Dropped={
                              self.dropped_packets}")

        except KeyboardInterrupt:
            self.print_final_stats()
            sock1.close()
            sock2.close()

    def start_tcp(self):
        def handle_connection(client_sock, from_client):
            dest_port = self.args.client2_listen if from_client == 'client1' else self.args.client1_listen
            try:
                dest_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                dest_sock.connect(('localhost', dest_port))
            except Exception as e:
                print(f"Connection failed: {e}")
                client_sock.close()
                return

            def forward(src, dst, direction):
                try:
                    while True:
                        data = src.recv(4096)
                        if not data:
                            break
                        with self.lock:
                            if random.random() < self.args.drop_rate:
                                self.dropped_packets += 1
                                print(
                                    f"Dropped {direction} packet ({len(data)} bytes)")
                            else:
                                dst.send(data)
                                self.sent_packets += 1
                                print(f"Forwarded {
                                      direction} packet ({len(data)} bytes)")
                            print(f"Stats: Sent={self.sent_packets}, Dropped={
                                  self.dropped_packets}")
                except Exception as e:
                    print(f"Connection error: {e}")
                finally:
                    src.close()
                    dst.close()

            threading.Thread(target=forward, args=(
                client_sock, dest_sock, 'outgoing')).start()
            threading.Thread(target=forward, args=(
                dest_sock, client_sock, 'incoming')).start()

        def start_server(port, client_name):
            server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            server.bind(('0.0.0.0', port))
            server.listen(5)
            print(f"Listening for {client_name} on port {port}...")
            try:
                while True:
                    client_sock, addr = server.accept()
                    print(f"Accepted {client_name} connection from {addr}")
                    threading.Thread(target=handle_connection, args=(
                        client_sock, client_name)).start()
            except KeyboardInterrupt:
                server.close()

        try:
            t1 = threading.Thread(target=start_server, args=(
                self.args.client1_recv, 'client1'))
            t2 = threading.Thread(target=start_server, args=(
                self.args.client2_recv, 'client2'))
            t1.start()
            t2.start()
            t1.join()
            t2.join()
        except KeyboardInterrupt:
            self.print_final_stats()

    def print_final_stats(self):
        print("\n=== Final Statistics ===")
        print(f"Total packets sent: {self.sent_packets}")
        print(f"Total packets dropped: {self.dropped_packets}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description='Network Proxy')
    parser.add_argument('--proto', choices=['tcp', 'udp'], required=True,
                        help='Protocol mode (tcp/udp)')
    parser.add_argument('--client1-listen', type=int, required=True,
                        help='Port where Client2 should listen (Proxy sends Client1 data here)')
    parser.add_argument('--client1-recv', type=int, required=True,
                        help='Port where Proxy receives Client1 data')
    parser.add_argument('--client2-listen', type=int, required=True,
                        help='Port where Client1 should listen (Proxy sends Client2 data here)')
    parser.add_argument('--client2-recv', type=int, required=True,
                        help='Port where Proxy receives Client2 data')
    parser.add_argument('--drop-rate', type=float, required=True,
                        help='Packet drop rate (0.0-1.0)')

    args = parser.parse_args()

    if not (0 <= args.drop_rate <= 1):
        print("Error: Drop rate must be between 0 and 1")
        sys.exit(1)

    proxy = Proxy(args)
    try:
        proxy.start()
    except KeyboardInterrupt:
        proxy.print_final_stats()
        sys.exit(0)
