#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
import struct
import argparse

def build_nla(attr_id, fmt, *values, nested=False):
    payload = struct.pack(fmt, *values)
    attr_id |= 0x8000 if nested else 0x0000
    nla_len = len(payload) + 4
    pad_len = (4 - (nla_len % 4)) % 4
    return struct.pack('<HH', nla_len, attr_id) + payload + (b'\x00' * pad_len)

def get_channel_hop_cmd(channels, switch_time, tsf_offset=None, bw_val=2):
    payload = bytearray()
    payload += build_nla(1, '<B', len(channels)) # LIST_LEN
    payload += build_nla(2, '<I', 0)             # NEXT_IDX
    payload += build_nla(3, '<I', 64)            # DWELL_TIME

    # Build list entries
    nested_list = bytearray()
    for idx, freq in enumerate(channels, start=1):
        entry = bytearray()
        entry += build_nla(1, '<I', freq)                    # FREQ
        entry += build_nla(2, '<I', bw_val)                  # BW
        entry += build_nla(3, '<I', 0 if freq > 0 else 1)    # ROLE (STA = 1, NOP = 0)
        nested_list += build_nla(idx, '{}s'.format(len(entry)), entry, nested=True)

    payload += build_nla(5, '{}s'.format(len(nested_list)), nested_list) # LIST

    payload += build_nla(4, '<I', switch_time)   # SWITCH_TIME (Attr 4)

    if tsf_offset is not None:
        payload += build_nla(6, '<I', tsf_offset) # TSF_OFFSET (Attr 6)

    return "adb shell iw dev wonder0 vendor send 0x001A11 0x06 " + " ".join([f"0x{b:02x}" for b in payload])

def main():
    parser = argparse.ArgumentParser(description="Generate iw channel hopping command")
    parser.add_argument("freq", type=int, help="Target frequency in MHz (e.g., 5475)")
    parser.add_argument("percent", choices=['25', '37', '50', '75', '80', '90', '100'],
                        help="Select the channel schedule percentage (25, 37, 50, 75, 80, 90 or 100)")
    parser.add_argument("-s", "--switch-time", type=int, default=0,
                        help="Target switch time in TSF. Always included (default: 0).")
    parser.add_argument("-t", "--tsf-offset", type=int, default=None,
                        help="TSF offset in us (Attr 6). If provided, it will be added.")
    parser.add_argument("-b", "--bw", type=int, choices=[20, 40, 80, 160, 320], default=80,
                        help="Bandwidth in MHz (20, 40, 80, 160, 320). Default is 80.")

    args = parser.parse_args()
    f = args.freq

    cases = {
        '25':  [f, 0, 0, 0, 0, 0, 0, 0, 0, f, f, 0, 0, 0, 0, 0],
        '37':  [f, f, f, 0, 0, 0, 0, 0, 0, f, f, 0, 0, 0, 0, 0],
        '50':  [f, f, f, f, 0, 0, 0, 0, 0, f, f, f, 0, 0, 0, 0],
        '75':  [f, f, f, f, f, f, 0, 0, 0, f, f, f, f, f, 0, 0],
        '80':  [f, f, f, f, f, f, f, f, 0, f, f, f, f, f, 0, 0],
        '90':  [f, f, f, f, f, f, f, f, 0, f, f, f, f, f, f, 0],
        '100': [f, f, f, f, f, f, f, f, 0, f, f, f, f, f, f, f]
    }

    channels = cases[args.percent]

    bw_map = {20: 0, 40: 1, 80: 2, 160: 3, 320: 4}
    bw_val = bw_map[args.bw]

    print(f"# Generating command for {args.percent}% schedule on {f} MHz...")
    print(f"# Using SWITCH_TIME (Attr 4): {args.switch_time}")
    if args.tsf_offset is not None:
        print(f"# Using TSF_OFFSET (Attr 6): {args.tsf_offset}")
    print(f"# Using Bandwidth: {args.bw} MHz (Value: {bw_val})")

    print(f"# Channel List: {channels}")
    print(get_channel_hop_cmd(channels, args.switch_time, args.tsf_offset, bw_val))

if __name__ == '__main__':
    main()
