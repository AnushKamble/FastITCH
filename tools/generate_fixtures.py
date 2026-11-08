#!/usr/bin/env python3
"""
FastITCH Deterministic Fixture Generator
Generates synthetic NASDAQ TotalView-ITCH 5.0 BinaryFILE and PCAP/MoldUDP64 captures.
All generated fixtures are strictly labeled as SYNTHETIC.
"""

import os
import struct
import sys

def build_common_header(msg_type: str, locate: int, tracking: int, ts_nanos: int) -> bytes:
    # 1-byte type + 2-byte locate + 2-byte tracking + 6-byte timestamp = 11 bytes
    ts_bytes = ts_nanos.to_bytes(6, byteorder='big')
    return struct.pack('>BHH', ord(msg_type), locate, tracking) + ts_bytes

def build_msg_s(locate: int, tracking: int, ts: int, event_code: str) -> bytes:
    # Type 'S': 12 bytes
    return build_common_header('S', locate, tracking, ts) + event_code.encode('ascii')

def build_msg_r(locate: int, tracking: int, ts: int, symbol: str) -> bytes:
    # Type 'R': 39 bytes
    hdr = build_common_header('R', locate, tracking, ts)
    stock = f"{symbol:<8}".encode('ascii')[:8]
    # Rest of R fields (20 bytes): cat(1), fin(1), lot(4), only(1), cls(1), sub(2), auth(1), ss(1), ipo(1), tier(1), etp(1), fac(4), inv(1)
    tail = struct.pack('>ccIcc2scccccIc', b'Q', b'N', 100, b'N', b'C', b'SI', b'P', b'N', b'N', b'1', b'N', 1, b'N')
    return hdr + stock + tail

def build_msg_a(locate: int, tracking: int, ts: int, order_id: int, side: str, shares: int, symbol: str, price_raw: int) -> bytes:
    # Type 'A': 36 bytes
    hdr = build_common_header('A', locate, tracking, ts)
    stock = f"{symbol:<8}".encode('ascii')[:8]
    body = struct.pack('>QcI8sI', order_id, side.encode('ascii'), shares, stock, price_raw)
    return hdr + body[0:1] + body[1:2] + body[2:6] + stock + body[14:18] # order_id(8), side(1), shares(4), stock(8), price(4)
    # Wait, let's pack explicitly:
    # return hdr + struct.pack('>QcI', order_id, side.encode('ascii'), shares) + stock + struct.pack('>I', price_raw)

def make_add_order(locate: int, tracking: int, ts: int, order_id: int, side: str, shares: int, symbol: str, price_raw: int) -> bytes:
    hdr = build_common_header('A', locate, tracking, ts)
    stock = f"{symbol:<8}".encode('ascii')[:8]
    body = struct.pack('>QcI', order_id, side.encode('ascii'), shares) + stock + struct.pack('>I', price_raw)
    msg = hdr + body
    assert len(msg) == 36, f"Expected 36 bytes for 'A', got {len(msg)}"
    return msg

def make_add_order_mpid(locate: int, tracking: int, ts: int, order_id: int, side: str, shares: int, symbol: str, price_raw: int, mpid: str) -> bytes:
    msg_a = make_add_order(locate, tracking, ts, order_id, side, shares, symbol, price_raw)
    # Change type byte to 'F' and append 4-byte MPID = 40 bytes
    msg_f = b'F' + msg_a[1:] + f"{mpid:<4}".encode('ascii')[:4]
    assert len(msg_f) == 40, f"Expected 40 bytes for 'F', got {len(msg_f)}"
    return msg_f

def make_order_executed(locate: int, tracking: int, ts: int, order_id: int, shares: int, match_num: int) -> bytes:
    hdr = build_common_header('E', locate, tracking, ts)
    body = struct.pack('>QIQ', order_id, shares, match_num)
    msg = hdr + body
    assert len(msg) == 31, f"Expected 31 bytes for 'E', got {len(msg)}"
    return msg

def make_order_cancel(locate: int, tracking: int, ts: int, order_id: int, shares: int) -> bytes:
    hdr = build_common_header('X', locate, tracking, ts)
    body = struct.pack('>QI', order_id, shares)
    msg = hdr + body
    assert len(msg) == 23, f"Expected 23 bytes for 'X', got {len(msg)}"
    return msg

def make_order_delete(locate: int, tracking: int, ts: int, order_id: int) -> bytes:
    hdr = build_common_header('D', locate, tracking, ts)
    body = struct.pack('>Q', order_id)
    msg = hdr + body
    assert len(msg) == 19, f"Expected 19 bytes for 'D', got {len(msg)}"
    return msg

def make_order_replace(locate: int, tracking: int, ts: int, orig_id: int, new_id: int, shares: int, price_raw: int) -> bytes:
    hdr = build_common_header('U', locate, tracking, ts)
    body = struct.pack('>QQII', orig_id, new_id, shares, price_raw)
    msg = hdr + body
    assert len(msg) == 35, f"Expected 35 bytes for 'U', got {len(msg)}"
    return msg

def make_trade(locate: int, tracking: int, ts: int, order_id: int, side: str, shares: int, symbol: str, price_raw: int, match_num: int) -> bytes:
    hdr = build_common_header('P', locate, tracking, ts)
    stock = f"{symbol:<8}".encode('ascii')[:8]
    body = struct.pack('>QcI', order_id, side.encode('ascii'), shares) + stock + struct.pack('>IQ', price_raw, match_num)
    msg = hdr + body
    assert len(msg) == 44, f"Expected 44 bytes for 'P', got {len(msg)}"
    return msg

def generate_binaryfile_fixtures(out_dir: str):
    os.makedirs(out_dir, exist_ok=True)
    messages = [
        build_msg_s(1, 1, 34200000000000, 'O'),
        build_msg_r(1, 2, 34200000000000, 'AAPL'),
        build_msg_r(2, 3, 34200000000000, 'MSFT'),
        # AAPL Orders:
        make_add_order(1, 4, 34201000000000, 1001, 'B', 500, 'AAPL', 1500000),      # Bid 150.00 x 500
        make_add_order(1, 5, 34202000000000, 1002, 'B', 300, 'AAPL', 1495000),      # Bid 149.50 x 300
        make_add_order_mpid(1, 6, 34203000000000, 1003, 'S', 200, 'AAPL', 1505000, 'TOWR'), # Ask 150.50 x 200
        make_add_order(1, 7, 34204000000000, 1004, 'S', 400, 'AAPL', 1510000),      # Ask 151.00 x 400
        # Partial execution on Bid 1001: 200 shares filled
        make_order_executed(1, 8, 34205000000000, 1001, 200, 99001),
        # Partial cancel on Ask 1004: 100 shares canceled
        make_order_cancel(1, 9, 34206000000000, 1004, 100),
        # Replace Ask 1003 with 1005: 250 shares at 150.40
        make_order_replace(1, 10, 34207000000000, 1003, 1005, 250, 1504000),
        # Non-cross trade print
        make_trade(1, 11, 34208000000000, 1005, 'B', 100, 'AAPL', 1504000, 99002),
        build_msg_s(1, 12, 57600000000000, 'C') # End of Messages
    ]

    # 1. Complete Session BinaryFILE
    complete_path = os.path.join(out_dir, "synthetic_session.bin")
    with open(complete_path, "wb") as f:
        for msg in messages:
            f.write(struct.pack('>H', len(msg)))
            f.write(msg)
        # Clean terminal marker 0x0000
        f.write(b'\x00\x00')
    print(f"Generated: {complete_path} ({len(messages)} messages + EOS)")

    # 2. Incomplete / Truncated Session BinaryFILE (missing 0x0000 marker)
    incomplete_path = os.path.join(out_dir, "synthetic_incomplete.bin")
    with open(incomplete_path, "wb") as f:
        for msg in messages[:6]:
            f.write(struct.pack('>H', len(msg)))
            f.write(msg)
        # Ends without 0x0000
    print(f"Generated: {incomplete_path} (6 messages, no EOS)")

def generate_pcap_fixtures(out_dir: str):
    os.makedirs(out_dir, exist_ok=True)

    def write_pcap(filepath: str, packets_mold: list):
        # PCAP Global Header: magic 0xa1b2c3d4, v2.4, thiszone 0, sigfigs 0, snaplen 65535, network 1 (Ethernet)
        global_hdr = struct.pack('>IHHiIII', 0xa1b2c3d4, 2, 4, 0, 0, 65535, 1)

        with open(filepath, "wb") as f:
            f.write(global_hdr)
            sec = 1600000000
            usec = 1000

            for mold_hdr, itch_msgs in packets_mold:
                # Build MoldUDP64 payload
                mold_payload = mold_hdr
                for m in itch_msgs:
                    mold_payload += struct.pack('>H', len(m)) + m

                # UDP Header: src 5000, dst 5001, len 8 + payload_len, chksum 0
                udp_len = 8 + len(mold_payload)
                udp_hdr = struct.pack('>HHHH', 5000, 5001, udp_len, 0)

                # IPv4 Header: ver 4, IHL 5 (20 bytes), tos 0, total_len 20 + udp_len, id 1, flags/frag 0, ttl 64, proto 17 (UDP), chksum 0, src 192.168.1.1, dst 192.168.1.2
                ip_len = 20 + udp_len
                ip_hdr = struct.pack('>BBHHHBBH4s4s', 0x45, 0, ip_len, 1, 0, 64, 17, 0, b'\xc0\xa8\x01\x01', b'\xc0\xa8\x01\x02')

                # Ethernet Header: dest(6), src(6), ethertype 0x0800
                eth_hdr = b'\x00\x11\x22\x33\x44\x55\x66\x77\x88\x99\xaa\xbb\x08\x00'

                packet_bytes = eth_hdr + ip_hdr + udp_hdr + mold_payload

                # PCAP Packet Record Header: sec, usec, incl_len, orig_len
                rec_hdr = struct.pack('>IIII', sec, usec, len(packet_bytes), len(packet_bytes))
                f.write(rec_hdr)
                f.write(packet_bytes)
                usec += 1000

    # Mold sessions
    sess_id = b"SYNTHSESS1" # 10 bytes

    # Packet 1: Seq 1, count 2
    hdr1 = sess_id + struct.pack('>QH', 1, 2)
    msgs1 = [
        build_msg_s(1, 1, 34200000000000, 'O'),
        build_msg_r(1, 2, 34200000000000, 'AAPL')
    ]

    # Packet 2: Heartbeat (seq 3, count 0)
    hdr2 = sess_id + struct.pack('>QH', 3, 0)
    msgs2 = []

    # Packet 3: Seq 3, count 2
    hdr3 = sess_id + struct.pack('>QH', 3, 2)
    msgs3 = [
        make_add_order(1, 3, 34201000000000, 2001, 'B', 100, 'AAPL', 1500000),
        make_add_order(1, 4, 34202000000000, 2002, 'S', 100, 'AAPL', 1505000)
    ]

    # Packet 4: End of session (count 0xFFFF)
    hdr4 = sess_id + struct.pack('>QH', 5, 0xFFFF)
    msgs4 = []

    # Valid PCAP file
    valid_pcap = os.path.join(out_dir, "synthetic_moldudp64.pcap")
    write_pcap(valid_pcap, [(hdr1, msgs1), (hdr2, msgs2), (hdr3, msgs3), (hdr4, msgs4)])
    print(f"Generated: {valid_pcap} (complete MoldUDP64 capture)")

    # PCAP with deliberate sequence gap:
    # Packet 1: Seq 1, count 2 (expects next seq = 3)
    # Packet 2: Seq 10, count 1 (gap of 7 messages!)
    hdr_gap = sess_id + struct.pack('>QH', 10, 1)
    msgs_gap = [make_add_order(1, 10, 34205000000000, 2005, 'B', 100, 'AAPL', 1500000)]
    gap_pcap = os.path.join(out_dir, "synthetic_gap.pcap")
    write_pcap(gap_pcap, [(hdr1, msgs1), (hdr_gap, msgs_gap)])
    print(f"Generated: {gap_pcap} (MoldUDP64 capture with sequence gap)")

if __name__ == "__main__":
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "fixtures"
    generate_binaryfile_fixtures(out_dir)
    generate_pcap_fixtures(out_dir)
