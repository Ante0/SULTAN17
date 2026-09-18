#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0
"""
Usage:
    python3 channel_offset_monitor.py
"""
import subprocess
import time
import re
import os
import math
import argparse
import csv

# Configuration
DEVICE_SERIAL = None
REQUEST_DEBUGFS_PATH = "/sys/kernel/debug/wonder/channel_schedule_request"
REPORT_DEBUGFS_PATH = "/sys/kernel/debug/wonder/channel_status_report"
POLL_INTERVAL = 1  # Seconds
HISTORICAL_OFFSETS = []



# ANSI Colors
C_RED = "\033[91m"
C_GREEN = "\033[92m"
C_YELLOW = "\033[93m"
C_CYAN = "\033[96m"
C_RESET = "\033[0m"

def adb_command(args, check=True):
    """Executes an ADB command and returns the decoded output."""
    base_cmd = ["adb"]
    if DEVICE_SERIAL:
        base_cmd.extend(["-s", DEVICE_SERIAL])
    base_cmd.extend(args)
    result = subprocess.run(base_cmd, capture_output=True, check=check, text=True)
    return result.stdout

def read_debugfs_file(path):
    """Reads a debugfs file content via adb."""
    try:
        return adb_command(["shell", "cat", path])
    except Exception:
        return ""

def parse_channel_schedule_request(content):
    data = {}
    data['channel_list'] = []
    tsf_match = re.search(r"Target Switch TSF:\s+(0x[0-9a-fA-F]+)", content)
    dwell_match = re.search(r"Dwell Time \(TU\):\s+(\d+)", content)
    len_match = re.search(r"Channel List Length:\s+(\d+)", content)
    next_idx_match = re.search(r"Next Channel Index:\s+(\d+)", content)

    data['target_switch_tsf'] = int(tsf_match.group(1), 16) if tsf_match else 0
    data['dwell_tu'] = int(dwell_match.group(1)) if dwell_match else 64
    data['length'] = int(len_match.group(1)) if len_match else 16
    data['next_index'] = int(next_idx_match.group(1)) if next_idx_match else 0

    entry_re = re.compile(r"\[\s*(\d+)\]\s+Freq:\s+(\d+)\s+MHz,\s+BW:\s+(\d+),\s+Role:\s+(\d+)")
    temp_list = [{} for _ in range(data['length'])]
    for line in content.splitlines():
        match = entry_re.search(line)
        if match:
            idx = int(match.group(1))
            if 0 <= idx < data['length']:
                temp_list[idx] = {
                    "index": idx,
                    "freq": int(match.group(2)),
                    "role": int(match.group(4)),
                }
    data['channel_list'] = temp_list
    return data

def parse_channel_status_report(content):
    data = {"status": [{} for _ in range(16)]}
    tsf_match = re.search(r"Current Hopping Request TSF:\s+(0x[0-9a-fA-F]+)", content)
    idx_match = re.search(r"Current Channel Index:\s+(\d+)", content)
    len_match = re.search(r"Channel Status Length:\s+(\d+)", content)

    data['current_hopping_request_tsf'] = int(tsf_match.group(1), 16) if tsf_match else 0
    data['current_channel_index'] = int(idx_match.group(1)) if idx_match else -1
    data['channel_status_len'] = int(len_match.group(1)) if len_match else 0

    slot_index_freq_re = re.compile(r"\[\s*(\d+)\]\s+Freq:\s+(\d+)")
    switch_tsf_re = re.compile(r"Switch TSF:\s+(0x[0-9a-fA-F]+)")
    start_tsf_re = re.compile(r"Start TSF:\s+(0x[0-9a-fA-F]+)")
    end_tsf_re = re.compile(r"End TSF:\s+(0x[0-9a-fA-F]+)")
    tx_re = re.compile(r"TX:\s+(\d+)\s+frames,\s+(\d+)\s+bytes")
    rx_re = re.compile(r"RX:\s+(\d+)\s+frames,\s+(\d+)\s+bytes")

    current_slot = -1
    for line in content.splitlines():
        line = line.strip()
        m = slot_index_freq_re.match(line)
        if m:
            current_slot = int(m.group(1))
            if 0 <= current_slot < data['channel_status_len']:
                 data['status'][current_slot]['index'] = current_slot
                 data['status'][current_slot]['freq'] = int(m.group(2))
            continue
        if not (0 <= current_slot < data['channel_status_len']): continue
        slot = data['status'][current_slot]
        if m := switch_tsf_re.search(line): slot['channel_switch_tsf'] = int(m.group(1), 16)
        elif m := start_tsf_re.search(line): slot['channel_start_tsf'] = int(m.group(1), 16)
        elif m := end_tsf_re.search(line): slot['channel_end_tsf'] = int(m.group(1), 16)
        elif m := tx_re.search(line):
            slot['tx_frames'] = int(m.group(1)); slot['tx_bytes'] = int(m.group(2))
        elif m := rx_re.search(line):
            slot['rx_frames'] = int(m.group(1)); slot['rx_bytes'] = int(m.group(2))
    return data

def tsf_diff(t1, t2):
    """Calculate t1 - t2 handling wraparound for u32."""
    return (t1 - t2) & 0xFFFFFFFF

def to_signed(offset):
    if offset > 0x7FFFFFFF:
        return offset - 0x100000000
    return offset

def get_theoretical_times(base_tsf, dwell_us, num_slots, channel_list, target_idx, cycle_num):
    """Calculates theoretical times for a specific slot index in a specific cycle."""
    cycle_base_tsf = base_tsf + cycle_num * (num_slots * dwell_us)

    expected_start_tsf = cycle_base_tsf + target_idx * dwell_us
    expected_end_tsf = expected_start_tsf + dwell_us

    current_role = channel_list[target_idx]['role']

    expected_switch_tsf = base_tsf # Default for cycle 0, idx 0
    # Look back up to one full cycle to find the role change
    for i in range(num_slots):
        check_idx = target_idx - i
        k = cycle_num

        if check_idx < 0:
            if k == 0:
                 expected_switch_tsf = base_tsf
                 break
            k -= 1
            check_idx += num_slots

        c_base = base_tsf + k * (num_slots * dwell_us)
        role = channel_list[check_idx]['role']

        if check_idx == 0:
            if k == 0 or role != channel_list[num_slots - 1]['role']:
                expected_switch_tsf = c_base
                break
        else:
            prev_idx = (check_idx - 1 + num_slots) % num_slots
            prev_role = channel_list[prev_idx]['role']
            if role != prev_role:
                expected_switch_tsf = c_base + check_idx * dwell_us
                break
    return {
        "switch": expected_switch_tsf,
        "start": expected_start_tsf,
        "end": expected_end_tsf
    }



def display_offset_report(request_data, report_data, dashboard_mode=False, csv_path=None):
    cols = [
        # (header_name, width, header_align, value_align)
        ("Slot", 4, "^", "^"),
        ("Role", 4, "^", "^"),
        ("Actual Switch", 13, "^", "^"),
        ("Expected Switch", 15, "^", "^"),
        ("Switch Offset", 20, "^", "^"),
        ("Actual Start", 12, "^", "^"),
        ("Expected Start", 14, "^", "^"),
        ("Start Offset", 20, "^", "^"),
        ("Actual End", 10, "^", "^"),
        ("Expected End", 12, "^", "^"),
        ("End Offset", 20, "^", "^"),
    ]

    header_parts = []
    for name, width, h_align, v_align in cols:
        header_parts.append(f"{name:{h_align}{width}}")
    header_str = " | ".join(header_parts)
    border_len = len(header_str)

    if dashboard_mode:
        print("\033[H\033[2J", end="")

    print("\n" + "=" * border_len)
    print(" Channel Offset Report")
    print("=" * border_len)

    daemon_intended_base_tsf = request_data.get('target_switch_tsf', 0)
    dwell_tu = request_data.get('dwell_tu', 64)
    num_slots = request_data.get('length', 16)
    channel_list = request_data.get('channel_list', [])
    next_index = request_data.get('next_index', 0)
    current_fw_idx = report_data.get('current_channel_index', -1)

    if not channel_list: print(" Error: Channel list empty."); return
    if len(channel_list) != num_slots: print(f" Error: Channel list length mismatch"); return

    dwell_us = dwell_tu * 1024
    cycle_us = num_slots * dwell_us
    logical_cycle0_start = daemon_intended_base_tsf - (next_index * dwell_us)

    cycle_tu = cycle_us // 1024
    print(f"Target Switch TSF   : 0x{daemon_intended_base_tsf:08x}")
    print(f"Slot Dwell Time     : {dwell_tu} TU")
    print(f"Schedule Cycle Time : {cycle_tu} TU")
    print(f"Schedule Length     : {num_slots} Slots")
    print(f"Current Active Slot : {current_fw_idx}")
    C_GREY = "\033[90m"
    layout_parts = []
    for idx in range(num_slots):
        role = channel_list[idx]['role']
        role_str = "STA" if role == 1 else "NOP" if role == 0 else "UNK"
        if role == 1:
            colored_role = f"{C_CYAN}{role_str}{C_RESET}"
        elif role == 0:
            colored_role = f"{C_GREY}{role_str}{C_RESET}"
        else:
            colored_role = f"{C_RED}{role_str}{C_RESET}"

        if idx == current_fw_idx:
            slot_str = f"\033[1m{C_YELLOW}▶{colored_role}{C_YELLOW}◀\033[0m"
        else:
            slot_str = f"[{colored_role}]"
        layout_parts.append(slot_str)
    print(f"Schedule Layout     : {' '.join(layout_parts)}")
    print("-" * border_len)
    print(header_str)
    print("-" * border_len)

    if current_fw_idx == -1:
        print("Error: Invalid current_fw_idx from report.")
        return

    # Determine the cycle number of the current_fw_idx
    status_curr = report_data["status"][current_fw_idx]
    if not status_curr or not status_curr.get('channel_start_tsf'):
         print(f"Error: Missing data for current_fw_idx {current_fw_idx}")
         return
    actual_start_curr = status_curr['channel_start_tsf']
    time_since_base_curr = tsf_diff(actual_start_curr, logical_cycle0_start)
    k_current = time_since_base_curr // cycle_us

    def get_offset_color(pct):
        if abs(pct) > 5.0:
            return C_RED
        elif abs(pct) > 1.5:
            return C_YELLOW
        return C_GREEN

    max_abs_st_offset = 0.0
    report_slots = report_data.get("status", [])
    for i in range(report_data.get('channel_status_len', 0)):
        status = report_slots[i]
        if not status or not status.get('channel_switch_tsf'): continue

        idx = status.get("index", i)
        actual_switch = status.get("channel_switch_tsf", 0)
        actual_start = status.get("channel_start_tsf", 0)
        actual_end = status.get("channel_end_tsf", 0)
        freq = status.get("freq", 0)

        k = k_current
        if idx > current_fw_idx:
            if k > 0: k -= 1

        theoretical = get_theoretical_times(logical_cycle0_start, dwell_us, num_slots, channel_list, idx, k)
        exp_switch = theoretical['switch']
        exp_start = theoretical['start']
        exp_end = theoretical['end']

        switch_offset = to_signed(tsf_diff(actual_switch, exp_switch))
        start_offset = to_signed(tsf_diff(actual_start, exp_start))
        
        # Convert offsets to TU (1 TU = 1024 microseconds)
        sw_offset_tu = switch_offset / 1024.0
        st_offset_tu = start_offset / 1024.0
        max_abs_st_offset = max(max_abs_st_offset, abs(st_offset_tu))

        sw_pct = (sw_offset_tu / dwell_tu) * 100.0
        st_pct = (st_offset_tu / dwell_tu) * 100.0

        role = channel_list[idx]['role']
        role_str = "STA" if role == 1 else "NOP" if role == 0 else "UNK"

        # Smart Filter: Detect consecutive same-role slot to suppress false warning coloring
        prev_idx = (idx - 1 + num_slots) % num_slots
        prev_role = channel_list[prev_idx]['role']
        if role == prev_role:
            sw_offset_str = "N/A"
            sw_color = C_RESET
        else:
            sw_offset_str = f"{sw_offset_tu:+.2f} TU ({sw_pct:+.2f}%)"
            sw_color = get_offset_color(sw_pct)

        st_color = get_offset_color(st_pct)

        if actual_end == 0:
            end_offset_str = "N/A"
            end_color = C_RESET
        else:
            end_offset = to_signed(tsf_diff(actual_end, exp_end))
            end_offset_tu = end_offset / 1024.0
            end_pct = (end_offset_tu / dwell_tu) * 100.0
            end_offset_str = f"{end_offset_tu:+.2f} TU ({end_pct:+.2f}%)"
            end_color = get_offset_color(end_pct)

        row_vals = [
            f"{idx}",
            f"{role_str}",
            f"0x{actual_switch:08x}",
            f"0x{exp_switch & 0xFFFFFFFF:08x}",
            sw_offset_str,
            f"0x{actual_start:08x}",
            f"0x{exp_start & 0xFFFFFFFF:08x}",
            f"{st_offset_tu:+.2f} TU ({st_pct:+.2f}%)",
            f"0x{actual_end:08x}",
            f"0x{exp_end & 0xFFFFFFFF:08x}",
            f"{end_offset_str.strip()}",
        ]

        colors = [
            C_RESET,
            C_RESET,
            C_RESET,
            C_RESET,
            sw_color,
            C_RESET,
            C_RESET,
            st_color,
            C_RESET,
            C_RESET,
            end_color,
        ]

        row_parts = []
        for (name, width, h_align, v_align), val, col_color in zip(cols, row_vals, colors):
            formatted_val = f"{val:{v_align}{width}}"
            row_parts.append(f"{col_color}{formatted_val}{C_RESET}")

        row_str = " | ".join(row_parts)
        print(row_str)

        # CSV Telemetry Logging
        if csv_path:
            try:
                with open(csv_path, "a", newline="") as f:
                    writer = csv.writer(f)
                    timestamp = time.strftime("%Y-%m-%d %H:%M:%S")
                    writer.writerow([
                        timestamp, idx, role_str,
                        "N/A" if role == prev_role else f"{sw_offset_tu:+.2f}",
                        "N/A" if role == prev_role else f"{sw_pct:+.2f}",
                        f"{st_offset_tu:+.2f}", f"{st_pct:+.2f}",
                        "N/A" if actual_end == 0 else f"{end_offset_tu:+.2f}",
                        "N/A" if actual_end == 0 else f"{end_pct:+.2f}"
                    ])
            except Exception:
                pass

    global HISTORICAL_OFFSETS
    HISTORICAL_OFFSETS.append(max_abs_st_offset)
    if len(HISTORICAL_OFFSETS) > 40:
        HISTORICAL_OFFSETS.pop(0)

    history_max = max(HISTORICAL_OFFSETS) if HISTORICAL_OFFSETS else 0.0
    # Cap the ceiling at 15.0 TU to prevent giant spikes (outliers) from crushing the scale
    ceiling = min(max(history_max, 1.0), 15.0)
    capped_suffix = " (Capped)" if history_max > 15.0 else ""

    def make_sparkline(history, scale_limit):
        blocks = [" ", "▂", "▃", "▄", "▅", "▆", "▇", "█"]
        spark = []
        for v in history:
            idx = int(min(v, scale_limit) / scale_limit * 7)
            spark.append(blocks[idx])
        return "".join(spark)

    sparkline_str = make_sparkline(HISTORICAL_OFFSETS, ceiling)
    print()
    print(f"Max Start Offset History (Last 40 Polls) [Scale: 0.0 - {ceiling:.2f} TU{capped_suffix}]: {sparkline_str} (Current Max: {max_abs_st_offset:+.2f} TU)")
    print("=" * border_len + "\n")

def main():
    parser = argparse.ArgumentParser(description="Channel Offset Monitor Tool")
    parser.add_argument("-s", "--serial", help="ADB device serial number")
    parser.add_argument("-i", "--interval", type=float, default=1.0, help="Polling interval in seconds (default: 1.0)")
    parser.add_argument("-d", "--dashboard", action="store_true", help="Enable dashboard mode (clear terminal before updating)")
    parser.add_argument("--csv", help="Path to export telemetry logs to a CSV file")
    args = parser.parse_args()

    global DEVICE_SERIAL
    if args.serial:
        DEVICE_SERIAL = args.serial

    poll_interval = args.interval
    dashboard_mode = args.dashboard
    csv_path = args.csv

    if csv_path:
        if not os.path.exists(csv_path):
            try:
                with open(csv_path, "w", newline="") as f:
                    writer = csv.writer(f)
                    writer.writerow([
                        "Timestamp", "Slot", "Role",
                        "Switch_Offset_TU", "Switch_Offset_Pct",
                        "Start_Offset_TU", "Start_Offset_Pct",
                        "End_Offset_TU", "End_Offset_Pct"
                    ])
            except Exception as e:
                print(f"Error initializing CSV file: {e}")
                return

    print("[Main] Starting Channel Status Report Monitor...")
    device_connected = True
    while True:
        try:
            request_content = read_debugfs_file(REQUEST_DEBUGFS_PATH)
            report_content = read_debugfs_file(REPORT_DEBUGFS_PATH)

            if not request_content or not report_content:
                if device_connected:
                    device_connected = False
                    if dashboard_mode:
                        print("\033[H\033[2J", end="")
                    print("\n" + "=" * 80)
                    print(" [ADB Status] Device Offline or Debugfs Unmounted")
                    print("  - Please check USB connection and root status ('adb root')")
                    print("=" * 80 + "\n")
                time.sleep(poll_interval)
                continue

            device_connected = True
            request_data = parse_channel_schedule_request(request_content)
            report_data = parse_channel_status_report(report_content)

            display_offset_report(request_data, report_data, dashboard_mode=dashboard_mode, csv_path=csv_path)
        except Exception as e:
            if device_connected:
                print(f"[Main] Error: {e}")

        time.sleep(poll_interval)

if __name__ == "__main__":
    main()
