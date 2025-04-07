#!/bin/python3
import argparse
import random
import socket
import sys
import threading
from collections import defaultdict

class Proxy:
    def __init__(self, client_port, server_port, protocol='tcp', drop_percent=0, seed=None):
        self.client_port = client_port
        self.server_port = server_port
        self.protocol = protocol.lower()
        self.drop_percent = drop_percent
        self.seed = seed
        
        # Statistics
        self.stats = {
            'total_packets': 0,
            'dropped_packets': 0,
            'proxied_packets': 0,
            'client_to_server': 0,
            'server_to_client': 0
        }
        
        # Lock for thread-safe statistics updates
        self.stats_lock = threading.Lock()
        
        # Initialize random seed if provided
        if self.seed is not None:
            random.seed(self.seed)
        
        # Create sockets based on protocol
        if self.protocol == 'tcp':
            self.socket_type = socket.SOCK_STREAM
        elif self.protocol == 'udp':
            self.socket_type = socket.SOCK_DGRAM
        else:
            raise ValueError("Protocol must be either 'tcp' or 'udp'")
    
    def start(self):
        print(f"Starting {self.protocol.upper()} proxy...")
        print(f"Client port: {self.client_port}")
        print(f"Server port: {self.server_port}")
        print(f"Packet drop percentage: {self.drop_percent}%")
        if self.seed is not None:
            print(f"Random seed: {self.seed}")
        
        try:
            # Create proxy socket
            self.proxy_socket = socket.socket(socket.AF_INET, self.socket_type)
            self.proxy_socket.bind(('0.0.0.0', 6000))
            
            if self.protocol == 'tcp':
                self.proxy_socket.listen(5)
                print("Proxy listening on TCP port 6000...")
                self.handle_tcp()
            else:
                print("Proxy listening on UDP port 6000...")
                self.handle_udp()
        except Exception as e:
            print(f"Error starting proxy: {e}")
        finally:
            self.proxy_socket.close()
    
    def handle_tcp(self):
        try:
            while True:
                client_sock, client_addr = self.proxy_socket.accept()
                print(f"TCP connection from {client_addr}")
                
                # Determine if this is client or server based on port
                if client_addr[1] == self.client_port:
                    # Client connecting to proxy
                    server_sock = socket.socket(socket.AF_INET, self.socket_type)
                    server_sock.connect(('localhost', self.server_port))
                    
                    # Start threads to handle bidirectional communication
                    threading.Thread(
                        target=self.tcp_forward,
                        args=(client_sock, server_sock, 'client_to_server')
                    ).start()
                    threading.Thread(
                        target=self.tcp_forward,
                        args=(server_sock, client_sock, 'server_to_client')
                    ).start()
                else:
                    # Shouldn't happen in normal operation
                    print(f"Unexpected connection from port {client_addr[1]}")
                    client_sock.close()
        except KeyboardInterrupt:
            print("\nShutting down proxy...")
            self.print_stats()
    
    def tcp_forward(self, src, dst, direction):
        try:
            while True:
                data = src.recv(4096)
                if not data:
                    break
                
                with self.stats_lock:
                    self.stats['total_packets'] += 1
                    self.stats[direction] += 1
                
                # Decide whether to drop this packet
                if random.randint(1, 100) <= self.drop_percent:
                    with self.stats_lock:
                        self.stats['dropped_packets'] += 1
                    print(f"Dropped TCP packet from {direction}")
                    continue
                
                dst.send(data)
                with self.stats_lock:
                    self.stats['proxied_packets'] += 1
        except ConnectionResetError:
            pass
        finally:
            src.close()
            dst.close()
    
    def handle_udp(self):
        try:
            while True:
                data, addr = self.proxy_socket.recvfrom(4096)
                
                with self.stats_lock:
                    self.stats['total_packets'] += 1
                
                # Determine direction based on source port
                if addr[1] == self.client_port:
                    direction = 'client_to_server'
                    target_port = self.server_port
                else:
                    direction = 'server_to_client'
                    target_port = self.client_port
                
                with self.stats_lock:
                    self.stats[direction] += 1
                
                # Decide whether to drop this packet
                if random.randint(1, 100) <= self.drop_percent:
                    with self.stats_lock:
                        self.stats['dropped_packets'] += 1
                    print(f"Dropped UDP packet from {direction}")
                    continue
                
                # Forward the packet
                self.proxy_socket.sendto(data, ('localhost', target_port))
                with self.stats_lock:
                    self.stats['proxied_packets'] += 1
        except KeyboardInterrupt:
            print("\nShutting down proxy...")
            self.print_stats()
    
    def print_stats(self):
        print("\n--- Proxy Statistics ---")
        print(f"Total packets: {self.stats['total_packets']}")
        print(f"Proxied packets: {self.stats['proxied_packets']}")
        print(f"Dropped packets: {self.stats['dropped_packets']} "
              f"({self.stats['dropped_packets'] / max(1, self.stats['total_packets']) * 100:.2f}%)")
        print(f"Client->Server packets: {self.stats['client_to_server']}")
        print(f"Server->Client packets: {self.stats['server_to_client']}")

def main():
    parser = argparse.ArgumentParser(description="Python Proxy with Packet Drop Functionality")
    parser.add_argument('--client-port', type=int, required=True, help="Port for client connections")
    parser.add_argument('--server-port', type=int, required=True, help="Port for server connections")
    parser.add_argument('--protocol', type=str, default='tcp', choices=['tcp', 'udp'], 
                       help="Protocol to use (tcp or udp)")
    parser.add_argument('--drop-percent', type=int, default=0, 
                       help="Percentage of packets to drop (0-100)")
    parser.add_argument('--seed', type=int, default=None, 
                       help="Random seed for reproducible packet drops")
    
    args = parser.parse_args()
    
    if not (0 <= args.drop_percent <= 100):
        print("Error: drop-percent must be between 0 and 100")
        sys.exit(1)
    
    proxy = Proxy(
        client_port=args.client_port,
        server_port=args.server_port,
        protocol=args.protocol,
        drop_percent=args.drop_percent,
        seed=args.seed
    )
    
    proxy.start()

if __name__ == "__main__":
    main()
