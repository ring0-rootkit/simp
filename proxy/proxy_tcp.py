#!/usr/bin/env python3
import argparse
import random
import socket
import select
import sys
import threading
import struct
from collections import defaultdict

# Constants for TCP header flags
TCP_FIN = 0x01
TCP_SYN = 0x02
TCP_RST = 0x04
TCP_PSH = 0x08
TCP_ACK = 0x10
TCP_URG = 0x20


class Proxy:
    def __init__(self, args):
        self.args = args
        self.stats = {
            'sent': 0,
            'dropped_c1_to_c2': 0,
            'dropped_c2_to_c1': 0,
            'connections': defaultdict(int)
        }
        self.lock = threading.Lock()
        self.connections = {}  # Track active connections
        self.raw_socket = None

    def start(self):
        try:
            # Create raw socket (requires root privileges)
            self.raw_socket = socket.socket(
                socket.AF_INET, socket.SOCK_RAW, socket.IPPROTO_TCP)
            self.raw_socket.setsockopt(socket.IPPROTO_IP, socket.IP_HDRINCL, 1)

            print(f"Starting TCP proxy with packet loss simulation ({
                  self.args.drop_rate*100}%)")
            self.start_tcp_proxy()
        except PermissionError:
            print("Error: Raw socket operations require root privileges",
                  file=sys.stderr)
            sys.exit(1)
        except Exception as e:
            print(f"Error: {e}", file=sys.stderr)
            sys.exit(1)

    def parse_tcp_header(self, data):
        # Minimum TCP header is 20 bytes (without options)
        if len(data) < 40:  # IP header (20 bytes) + TCP header (20 bytes)
            return None

        # Skip IP header (first 20 bytes)
        tcp_header_raw = data[20:40]

        try:
            # Unpack TCP header (src_port, dest_port, seq, ack, offset_reserved_flags, window, checksum, urg_ptr)
            src_port, dest_port, seq, ack, offset_reserved_flags = struct.unpack(
                '!HHLLB', tcp_header_raw[:13])
            window, checksum, urg_ptr = struct.unpack(
                '!HHH', tcp_header_raw[12:18])

            data_offset = (offset_reserved_flags >> 4) * 4
            flags = offset_reserved_flags & 0x1F

            # Get payload if present
            payload = data[20 +
                           data_offset:] if len(data) > 20+data_offset else b''

            return {
                'src_port': src_port,
                'dest_port': dest_port,
                'seq': seq,
                'ack': ack,
                'flags': flags,
                'window': window,
                'payload': payload
            }
        except struct.error as e:
            print(f"Error unpacking TCP header: {e}")
            return None

    def build_tcp_packet(self, src_ip, dest_ip, tcp_header):
        # Build TCP header (20 bytes without options)
        offset_reserved_flags = (5 << 4) | (tcp_header['flags'] & 0x1F)

        # Pack the header fields
        tcp_header_raw = struct.pack('!HHLLBBH',
                                     tcp_header['src_port'],
                                     tcp_header['dest_port'],
                                     tcp_header['seq'],
                                     tcp_header['ack'],
                                     offset_reserved_flags,
                                     tcp_header['window'],
                                     0)  # Checksum placeholder

        # Add urgent pointer if needed
        if tcp_header['flags'] & TCP_URG:
            tcp_header_raw += struct.pack('!H', 0)  # Urgent pointer
        else:
            tcp_header_raw += struct.pack('!H', 0)  # Zero urgent pointer

        # Simple checksum (in production should include pseudo-header)
        checksum = 0
        tcp_header_raw = tcp_header_raw[:16] + \
            struct.pack('H', checksum) + tcp_header_raw[18:]

        return tcp_header_raw + tcp_header['payload']

    def forward_packet(self, data, direction):
        try:
            tcp_header = self.parse_tcp_header(data)
            if not tcp_header:
                return

            # Update connection tracking
            conn_key = (tcp_header['src_port'], tcp_header['dest_port'])
            if tcp_header['flags'] & TCP_SYN:
                with self.lock:
                    self.stats['connections'][conn_key] += 1

            # Determine if we should drop this packet
            drop_packet = random.random() < self.args.drop_rate

            with self.lock:
                if drop_packet:
                    if direction == 'c1_to_c2':
                        self.stats['dropped_c1_to_c2'] += 1
                    else:
                        self.stats['dropped_c2_to_c1'] += 1
                    print(f"Dropped {direction} packet (seq={tcp_header['seq']}, ack={
                          tcp_header['ack']}, flags={tcp_header['flags']})")
                    return

                self.stats['sent'] += 1

            # Modify packet to forward to the other client
            if direction == 'c1_to_c2':
                new_dest_port = self.args.client2_listen
                new_src_port = self.args.client1_recv
            else:
                new_dest_port = self.args.client1_listen
                new_src_port = self.args.client2_recv

            # Update TCP header with new ports
            modified_header = tcp_header.copy()
            modified_header['src_port'] = new_src_port
            modified_header['dest_port'] = new_dest_port

            # Rebuild packet
            new_packet = self.build_tcp_packet(
                '127.0.0.1', '127.0.0.1', modified_header)

            # Send the packet
            self.raw_socket.sendto(new_packet, ('127.0.0.1', new_dest_port))

            print(f"Forwarded {direction} packet (seq={tcp_header['seq']}, ack={
                  tcp_header['ack']}, flags={tcp_header['flags']})")

            # Handle connection teardown
            if tcp_header['flags'] & (TCP_FIN | TCP_RST):
                with self.lock:
                    if conn_key in self.stats['connections']:
                        self.stats['connections'][conn_key] -= 1
                        if self.stats['connections'][conn_key] <= 0:
                            del self.stats['connections'][conn_key]

        except Exception as e:
            print(f"Error processing packet: {e}")

    def start_tcp_proxy(self):
        print(f"Listening for Client1 on port {self.args.client1_recv}")
        print(f"Listening for Client2 on port {self.args.client2_recv}")
        print(f"Forwarding Client1 packets to port {self.args.client2_listen}")
        print(f"Forwarding Client2 packets to port {self.args.client1_listen}")

        try:
            while True:
                data, addr = self.raw_socket.recvfrom(65535)
                tcp_header = self.parse_tcp_header(data)
                if not tcp_header:
                    continue

                # Determine direction
                if tcp_header['dest_port'] == self.args.client1_recv:
                    direction = 'c2_to_c1'
                elif tcp_header['dest_port'] == self.args.client2_recv:
                    direction = 'c1_to_c2'
                else:
                    continue  # Not our traffic

                # Process packet in a separate thread
                threading.Thread(target=self.forward_packet,
                                 args=(data, direction)).start()

        except KeyboardInterrupt:
            self.print_final_stats()
        finally:
            if self.raw_socket:
                self.raw_socket.close()

    def print_stats(self):
        with self.lock:
            active_connections = sum(
                1 for v in self.stats['connections'].values() if v > 0)
            print(f"\n[Stats] Sent: {self.stats['sent']} | "
                  f"Dropped (C1->C2): {self.stats['dropped_c1_to_c2']} | "
                  f"Dropped (C2->C1): {self.stats['dropped_c2_to_c1']} | "
                  f"Active connections: {active_connections}")

    def print_final_stats(self):
        print("\n=== Final Statistics ===")
        with self.lock:
            print(f"Total packets sent: {self.stats['sent']}")
            print(
                f"Total packets dropped (Client1->Client2): {self.stats['dropped_c1_to_c2']}")
            print(
                f"Total packets dropped (Client2->Client1): {self.stats['dropped_c2_to_c1']}")
            print(f"Maximum concurrent connections: {
                  max(self.stats['connections'].values()) if self.stats['connections'] else 0}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description='TCP Proxy with Packet Loss Simulation')
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
        print("Error: Drop rate must be between 0 and 1", file=sys.stderr)
        sys.exit(1)

    random.seed(42)  # Fixed seed for reproducible behavior

    proxy = Proxy(args)
    try:
        proxy.start()
    except KeyboardInterrupt:
        proxy.print_final_stats()
        sys.exit(0)
