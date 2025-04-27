#!/bin/python3
import socket
import struct
import threading
import time

# Reliable UDP with stop-and-wait ARQ for sending a sequence of numbers
# Packet format: 4-byte sequence number only


class ReliableUDPClient:
    def __init__(self, server_addr, local_port=None, timeout=1.0, max_retries=5):
        self.server_addr = server_addr
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        if local_port is not None:
            # bind to specific client port
            self.sock.bind(("", local_port))
        self.timeout = timeout
        self.max_retries = max_retries

    def send_number(self, seq: int) -> bool:
        packet = struct.pack("!I", seq)
        retries = 0
        while retries < self.max_retries:
            # send packet
            self.sock.sendto(packet, self.server_addr)
            # wait for ACK
            self.sock.settimeout(self.timeout)
            try:
                ack_pkt, _ = self.sock.recvfrom(4)
                ack_seq = struct.unpack("!I", ack_pkt)[0]
                if ack_seq == seq:
                    print(f"[Client] ACK {ack_seq} received for seq {seq}.")
                    return True
            except socket.timeout:
                retries += 1
                print(f"[Client] Timeout for seq {seq}, retry {
                      retries}/{self.max_retries}")
        print(f"[Client] Failed to send seq {seq}: max retries reached.")
        return False

    def send_sequence(self, start: int, end: int):
        for seq in range(start, end + 1):
            if not self.send_number(seq):
                print(f"[Client] Aborting at seq {seq}.")
                break


class ReliableUDPServer:
    def __init__(self, listen_addr):
        self.listen_addr = listen_addr
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(self.listen_addr)
        self.expected_seq = 0
        self.running = False
        self.lock = threading.Lock()

    def start(self, handler):
        self.running = True
        print(f"[Server] Listening on {self.listen_addr}")
        while self.running:
            pkt, addr = self.sock.recvfrom(4096)
            seq, = struct.unpack("!I", pkt[:4])

            with self.lock:
                if seq == self.expected_seq:
                    handler(seq, addr)
                    print(f"[Server] Packet {seq} received and in order.")
                    self.expected_seq += 1
                else:
                    print(
                        f"[Server] Out-of-order packet {seq} (expected {self.expected_seq}), ignoring.")

                # ACK the last in-order seq (or repeat the last)
                ack_seq = seq if seq < self.expected_seq else self.expected_seq - 1
                ack_pkt = struct.pack("!I", ack_seq)
                self.sock.sendto(ack_pkt, addr)
                print(f"[Server] ACK {ack_seq} sent.")

    def stop(self):
        self.running = False
        self.sock.close()


if __name__ == "__main__":
    import sys

    mode = sys.argv[1]

    if mode == "server":
        if len(sys.argv) != 4:
            print("Usage: python reliable_udp.py server host port")
            sys.exit(1)
        host = sys.argv[2]
        port = int(sys.argv[3])

        def handle(seq, addr):
            print(f"[App] Received number {seq} from {addr}")

        server = ReliableUDPServer((host, port))
        try:
            server.start(handle)
        except KeyboardInterrupt:
            server.stop()

    elif mode == "client":
        if len(sys.argv) not in (5, 6):
            print(
                "Usage: python reliable_udp.py client server_host server_port client_port [start end]")
            sys.exit(1)
        server_host = sys.argv[2]
        server_port = int(sys.argv[3])
        client_port = int(sys.argv[4])
        # optional custom range
        if len(sys.argv) == 6:
            start, end = map(int, sys.argv[5].split(','))
        else:
            start, end = 0, 999

        client = ReliableUDPClient(
            (server_host, server_port), local_port=client_port)
        client.send_sequence(start, end)

    else:
        print("Usage: python reliable_udp.py [server|client] ...")
