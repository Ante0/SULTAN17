#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
Usage:
    adb shell iw dev wonder0 vendor recv 0x001a11 0x9 0x1 | python3 parse_channel_report.py
"""
import sys
import struct

# ==============================================================================
# Vendor Netlink Attributes Definitions (Mapping to wonder_ven_cmd.h)
# ==============================================================================
TOP_ATTR_REQ_TSF = 1
TOP_ATTR_CUR_IDX = 2
TOP_ATTR_LEN     = 3
TOP_ATTR_LIST    = 4

ENTRY_ATTR_SWITCH_TSF = 1
ENTRY_ATTR_FREQ       = 2
ENTRY_ATTR_START_TSF  = 3
ENTRY_ATTR_END_TSF    = 4
ENTRY_ATTR_TX_TRAFFIC_INDEX = 5
ENTRY_ATTR_RX_TRAFFIC_INDEX = 6

def parse_hex_dump(text):
    """
    Extract Hex Bytes from the output of iw vendor recv.
    Supports `vendor response: 08 00...` or `0000: 08 00...` formats.
    """
    hex_bytes = bytearray()
    for line in text.splitlines():
        line = line.strip()
        if not line:
            continue

        hex_str = ""
        # Match "vendor response: 00 00 00 ..."
        if line.startswith("vendor response:"):
            hex_str = line.split("vendor response:")[1]
        # Match "0000: 08 00 ..."
        elif ":" in line:
            parts = line.split(":", 1)
            # Ensure the part before the colon is an offset tag (all hex characters)
            if all(c in "0123456789abcdefABCDEF" for c in parts[0].strip()):
                hex_str = parts[1]

        # Convert hex strings to bytes
        for p in hex_str.split():
            if len(p) == 2 and all(c in "0123456789abcdefABCDEF" for c in p):
                hex_bytes.append(int(p, 16))

    return bytes(hex_bytes)

def parse_nla(data):
    attributes = []
    offset = 0
    while offset <= len(data) - 4:
        nla_len, nla_type = struct.unpack('<HH', data[offset:offset+4])

        # Skip 4 bytes if 00 00 00 00 padding is encountered
        if nla_len < 4:
            offset += 4
            continue

        payload_len = nla_len - 4
        if offset + nla_len > len(data):
            break  # Data is truncated

        real_type = nla_type & 0x3FFF
        payload = data[offset+4 : offset+4+payload_len]
        attributes.append((real_type, payload))

        # 4 Bytes alignment
        align_len = (nla_len + 3) & ~3
        offset += align_len

    return attributes

def decode_u16(data):
    return struct.unpack('<H', data)[0] if len(data) >= 2 else 0

def decode_u32(data):
    return struct.unpack('<I', data)[0] if len(data) >= 4 else 0

def decode_u64(data):
    return struct.unpack('<Q', data)[0] if len(data) >= 8 else 0

def print_channel_report(data):
    top_attrs = parse_nla(data)

    # [Auto-Unwrap Mechanism]
    # If the outermost layer has only one attribute, and it is not an expected TOP_ATTR
    # (e.g., 197 or 195), it means the Driver manually wrapped an extra layer of
    # NL80211_ATTR_VENDOR_DATA. We unwrap it automatically.
    expected_top_attrs = (TOP_ATTR_REQ_TSF, TOP_ATTR_CUR_IDX, TOP_ATTR_LEN, TOP_ATTR_LIST)
    if len(top_attrs) == 1 and top_attrs[0][0] not in expected_top_attrs:
        # Unwrap the outer wrapper (extract its payload and parse again)
        top_attrs = parse_nla(top_attrs[0][1])

    print("=" * 60)
    print(" Wonder Channel Status Report")
    print("=" * 60)

    for nla_type, payload in top_attrs:
        if nla_type == TOP_ATTR_REQ_TSF:
            print(f"Current Hopping Request TSF : 0x{decode_u32(payload):08x}")
        elif nla_type == TOP_ATTR_CUR_IDX:
            print(f"Current Channel Index       : {decode_u32(payload)}")
        elif nla_type == TOP_ATTR_LEN:
            print(f"Channel Status Length       : {decode_u32(payload)}")
        elif nla_type == TOP_ATTR_LIST:
            print("\nChannel Status List:")
            list_attrs = parse_nla(payload)
            for entry_idx, entry_payload in list_attrs:
                print(f"  [Channel Entry {entry_idx - 1}]")  # -1 because the index starts from 1
                entry_attrs = parse_nla(entry_payload)
                for e_type, e_payload in entry_attrs:
                    if e_type == ENTRY_ATTR_SWITCH_TSF:
                        print(f"      Switch TSF: 0x{decode_u32(e_payload):08x} ({decode_u32(e_payload)})")
                    elif e_type == ENTRY_ATTR_FREQ:
                        print(f"      Frequency : {decode_u32(e_payload)} MHz")
                    elif e_type == ENTRY_ATTR_START_TSF:
                        print(f"      Start TSF : 0x{decode_u32(e_payload):08x}")
                    elif e_type == ENTRY_ATTR_END_TSF:
                        print(f"      End TSF   : 0x{decode_u32(e_payload):08x}")
                    elif e_type == ENTRY_ATTR_TX_TRAFFIC_INDEX:
                        print(f"      TX Traffic Index : {decode_u16(e_payload)}")
                    elif e_type == ENTRY_ATTR_RX_TRAFFIC_INDEX:
                        print(f"      RX Traffic Index : {decode_u16(e_payload)}")
        else:
            print(f"Unknown Top Level Attr {nla_type} (length {len(payload)} bytes)")

    print("=" * 60)

def main():
    text_data = sys.stdin.read() if not sys.stdin.isatty() else ""
    if len(sys.argv) > 1:
        with open(sys.argv[1], 'r') as f:
            text_data = f.read()

    hex_data = parse_hex_dump(text_data)
    if not hex_data:
        print("Error: No valid hex dump found.")
        sys.exit(1)

    print_channel_report(hex_data)

if __name__ == "__main__":
    main()
