"""
BLE ADC Logger — GlassTENG nRF5340
====================================
Connects to the device, receives NUS TX notifications, parses binary
packets, and saves every sample to a CSV file.

Requirements:
    pip install bleak

Usage:
    python ble_logger.py                   # auto-names CSV with timestamp
    python ble_logger.py -o my_data.csv    # custom output file
    python ble_logger.py --name MyDevice   # if you renamed CONFIG_BT_DEVICE_NAME
"""

import argparse
import asyncio
import csv
import datetime
import struct
import sys
from pathlib import Path

from bleak import BleakClient, BleakScanner

# ── Device / BLE constants ─────────────────────────────────────────────────────

DEVICE_NAME = "GlassTENG_ADC"         # CONFIG_BT_DEVICE_NAME in prj.conf

# NUS TX characteristic (device → PC, notify)
NUS_TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

# ── Packet layout (must match ble_sender.c struct ble_adc_packet) ─────────────
#
#  Offset  Size  Field
#  ------  ----  -----
#    0      2    seq        uint16_t  rolling sequence number
#    2      1    count      uint8_t   samples in this packet (always 5)
#    3      4    t0_us      uint32_t  timestamp of first sample (µs)
#    7     30    raw[5][3]  int16_t   raw ADC values, row-major
#   ──────────────────
#   Total: 37 bytes

BATCH_SIZE       = 5
ADC_NUM_CHANNELS = 3

PACKET_FMT  = f"<HBI{BATCH_SIZE * ADC_NUM_CHANNELS}h"
PACKET_SIZE = struct.calcsize(PACKET_FMT)   # 37

# ── ADC → mV conversion (mirrors adc_raw_to_millivolts_dt) ───────────────────
#
#  overlay config:  gain = ADC_GAIN_1_6,  reference = ADC_REF_INTERNAL (600 mV)
#  full-scale input = 600 mV / (1/6) = 3600 mV
#  mv = raw * 3600 / 2^12

ADC_FULL_SCALE_MV = 3600.0
ADC_RESOLUTION    = 12


def raw_to_mv(raw: int) -> float:
    return raw * ADC_FULL_SCALE_MV / (1 << ADC_RESOLUTION)


# ── Packet parser ──────────────────────────────────────────────────────────────

def parse_packet(data: bytes) -> list[dict] | None:
    if len(data) < PACKET_SIZE:
        return None

    fields   = struct.unpack_from(PACKET_FMT, data)
    seq      = fields[0]
    count    = fields[1]
    t0_us    = fields[2]
    raw_flat = fields[3:]                  # BATCH_SIZE * ADC_NUM_CHANNELS values

    rows = []
    for i in range(min(count, BATCH_SIZE)):
        base = i * ADC_NUM_CHANNELS
        ch_raw = [raw_flat[base + ch] for ch in range(ADC_NUM_CHANNELS)]
        ch_mv  = [raw_to_mv(r)        for r  in ch_raw]
        rows.append({
            "packet_seq": seq,
            "sample_idx": i,
            "t0_us":      t0_us,
            "ch0_raw":    ch_raw[0],
            "ch1_raw":    ch_raw[1],
            "ch2_raw":    ch_raw[2],
            "ch0_mv":     f"{ch_mv[0]:.2f}",
            "ch1_mv":     f"{ch_mv[1]:.2f}",
            "ch2_mv":     f"{ch_mv[2]:.2f}",
        })
    return rows


CSV_FIELDS = [
    "packet_seq", "sample_idx", "t0_us",
    "ch0_raw", "ch1_raw", "ch2_raw",
    "ch0_mv",  "ch1_mv",  "ch2_mv",
]

# ── Main ───────────────────────────────────────────────────────────────────────

async def run(device_name: str, csv_path: Path) -> None:
    print(f"Scanning for '{device_name}' …")
    device = await BleakScanner.find_device_by_name(device_name, timeout=15.0)
    if device is None:
        print(f"ERROR: device '{device_name}' not found within 15 s.")
        sys.exit(1)

    print(f"Found: {device.address}")

    total_samples = 0
    dropped_pkts  = 0
    last_seq: int | None = None

    with open(csv_path, "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=CSV_FIELDS)
        writer.writeheader()

        def on_notify(sender, data: bytearray) -> None:
            nonlocal total_samples, dropped_pkts, last_seq

            rows = parse_packet(bytes(data))
            if rows is None:
                print(f"\n[WARN] unexpected packet size: {len(data)} bytes")
                return

            seq = rows[0]["packet_seq"]

            # Detect dropped packets via sequence number gap
            if last_seq is not None:
                gap = (seq - last_seq - 1) & 0xFFFF
                if gap:
                    dropped_pkts += gap
                    print(f"\n[WARN] {gap} packet(s) lost (seq {last_seq} → {seq})")
            last_seq = seq

            writer.writerows(rows)
            f.flush()
            total_samples += len(rows)

            print(
                f"\r  {total_samples:>7} samples  |  "
                f"{dropped_pkts} pkt(s) lost  |  "
                f"last seq={seq:<5}",
                end="",
                flush=True,
            )

        async with BleakClient(device) as client:
            print(f"Connected. Saving to '{csv_path}'  (Ctrl+C to stop)\n")
            await client.start_notify(NUS_TX_UUID, on_notify)

            try:
                while True:
                    await asyncio.sleep(0.5)
            except KeyboardInterrupt:
                pass

            await client.stop_notify(NUS_TX_UUID)

    print(f"\n\nDone. {total_samples} samples → {csv_path}")
    if dropped_pkts:
        print(f"Warning: {dropped_pkts} packet(s) lost during session.")


def main() -> None:
    parser = argparse.ArgumentParser(description="GlassTENG BLE ADC logger")
    parser.add_argument(
        "-o", "--output",
        help="CSV output file (default: adc_log_YYYYMMDD_HHMMSS.csv)",
    )
    parser.add_argument(
        "--name",
        default=DEVICE_NAME,
        help=f"BLE device name (default: {DEVICE_NAME})",
    )
    args = parser.parse_args()

    if args.output:
        csv_path = Path(args.output)
    else:
        ts = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
        csv_path = Path(f"adc_log_{ts}.csv")

    asyncio.run(run(args.name, csv_path))


if __name__ == "__main__":
    main()
