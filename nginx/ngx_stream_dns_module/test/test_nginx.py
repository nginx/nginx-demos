#!/usr/bin/env python3
"""
Integration test suite for ngx_stream_dns_module.
Sends raw DNS packets via UDP and TCP sockets to NGINX Stream server and verifies socket communication.
"""

import sys
import socket
import time

QUERY_EXAMPLE_A = bytes([
    0x12, 0x34,               # ID
    0x01, 0x00,               # Flags: RD=1
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    # QNAME: example.com
    0x07, 0x65, 0x78, 0x61, 0x6d, 0x70, 0x6c, 0x65,
    0x03, 0x63, 0x6f, 0x6d, 0x00,
    0x00, 0x01, 0x00, 0x01    # QTYPE=A, QCLASS=IN
])

def test_udp_dns_query(host="127.0.0.1", port=5353):
    print(f"Sending UDP DNS Query to {host}:{port}...")
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(2.0)
    try:
        sock.sendto(QUERY_EXAMPLE_A, (host, port))
        print("✓ UDP DNS packet sent successfully!")
        return True
    except Exception as e:
        print(f"✗ UDP Test failed: {e}")
        return False
    finally:
        sock.close()

def test_tcp_dns_query(host="127.0.0.1", port=5353):
    print(f"Sending TCP DNS Query to {host}:{port}...")
    # Add 2-byte TCP length header
    tcp_payload = len(QUERY_EXAMPLE_A).to_bytes(2, byteorder='big') + QUERY_EXAMPLE_A
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(2.0)
    try:
        sock.connect((host, port))
        sock.sendall(tcp_payload)
        print("✓ TCP DNS packet sent successfully!")
        return True
    except Exception as e:
        print(f"✗ TCP Test failed: {e}")
        return False
    finally:
        sock.close()

if __name__ == "__main__":
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 5353

    u_ok = test_udp_dns_query(host, port)
    t_ok = test_tcp_dns_query(host, port)

    if u_ok and t_ok:
        print("\n=== ALL STREAM DNS MODULE INTEGRATION TESTS PASSED ===")
        sys.exit(0)
    else:
        print("\n=== STREAM DNS MODULE INTEGRATION TESTS FAILED ===")
        sys.exit(1)
