#!/usr/bin/env python3
"""
Protobuf-based receiver for IEEE 802.15.4 KeyPress messages
"""

import socket
import struct
import sys
from keypress_pb2 import KeyPress

# Protocol message types
MSG_KEY_PRESS = 0x01
MSG_LIGHT_STATE = 0x02
MSG_ACK = 0x03
MSG_HEARTBEAT = 0x04

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
sock.bind(("0.0.0.0", 4210))

print("Listening on UDP 4210 for Protobuf-encoded messages...")

seen_seqs = set()  # track seen sequence numbers to detect duplicates

while True:
    data, addr = sock.recvfrom(1024)

    if len(data) < 7:
        print("  Packet too short, skipping")
        continue

    # Parse protocol header: version(1), type(1), seq(2), dev_id(1), payload_len(2)
    ver, mtype, seq, dev_id, plen = struct.unpack_from(">BBHBH", data)
    payload = data[7:7+plen]
    
    print(f"  ver={ver} type=0x{mtype:02X} seq={seq} dev={dev_id} payload_len={plen}")

    if mtype == MSG_KEY_PRESS:  # 0x01 - KEY_PRESS with Protobuf
        # Always ACK first, before anything else
        ack = struct.pack(">BBHBH", 1, MSG_ACK, seq, dev_id, 0)
        sock.sendto(ack, addr)
        print(f"  ACK sent for seq={seq}")

        # Duplicate check — same seq means retransmit, don't process twice
        if seq in seen_seqs:
            print(f"  Duplicate seq={seq}, ignoring")
            continue
        seen_seqs.add(seq)

        # Decode protobuf message
        try:
            keypress = KeyPress()
            keypress.ParseFromString(payload)
            
            state_str = "PRESSED" if keypress.state == 1 else "RELEASED"
            print(f"  KEYPRESS: key_id={keypress.key_id} "
                  f"state={state_str} "
                  f"ts={keypress.ts}ms")
        except Exception as e:
            print(f"  ERROR: Failed to decode protobuf: {e}")

    elif mtype == MSG_ACK:  # 0x03 - ACK
        try:
            ack_seq = struct.unpack(">H", payload[:2])[0] if len(payload) >= 2 else -1
            print(f"  ACK for seq={ack_seq}")
        except Exception as e:
            print(f"  ERROR: Failed to parse ACK: {e}")

    elif mtype == MSG_HEARTBEAT:  # 0x04 - HEARTBEAT (no ACK needed)
        print(f"  HEARTBEAT")

    elif mtype == MSG_LIGHT_STATE:  # 0x02 - LIGHT_STATE (if using protobuf)
        try:
            # Note: If LIGHT_STATE uses a different message type, 
            # import and decode it here
            print(f"  LIGHT_STATE (payload={payload.hex()})")
        except Exception as e:
            print(f"  ERROR: Failed to decode LIGHT_STATE: {e}")

    else:
        print(f"  Unknown type 0x{mtype:02X}, payload={payload.hex()}")
