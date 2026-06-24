# nRF5340 ADC BLE Logger

Firmware for the nRF5340-DK that samples three ADC channels at 100 Hz and streams the data to a PC over BLE using the Nordic UART Service (NUS). A Python script on the PC receives the notifications and saves them to CSV.

## Directory Structure

```
.
├── boards/
│   └── nrf5340dk_nrf5340_cpuapp.overlay   ADC pin, gain, and reference config
├── src/
│   ├── adc_sampler.h      ADC module interface and shared queue contract
│   ├── adc_sampler.c      ADC sampling implementation (queue producer)
│   ├── ble_sender.h       BLE module interface
│   ├── ble_sender.c       BLE NUS send implementation (queue consumer)
│   ├── main.c             Top-level wiring: init ADC, init BLE, start sampling
│   ├── main_original.c    Original nRF code based on older ESP32 program
│   └── adc_original.c     Original reference code
├── sysbuild/
│   └── ipc_radio/
│       └── prj.conf       Network core config for BLE radio
├── tools/
│   └── ble_logger.py      PC-side receiver script
├── sysbuild.conf          
├── prj.conf               Firmware Kconfig (ADC, BLE, NUS, logging)
└── CMakeLists.txt
```

## ADC and BLE Contract

The ADC module and BLE module are decoupled through a `k_msgq`. The BLE side has no knowledge of how ADC sampling is implemented.

### Shared data structure (`src/adc_sampler.h`)

```c
#define ADC_NUM_CHANNELS 3

struct adc_sample {
    uint32_t timestamp_us;          // microseconds since firmware boot
    int16_t  raw[ADC_NUM_CHANNELS]; // 12-bit raw ADC values
    int32_t  mv[ADC_NUM_CHANNELS];  // converted voltage in mV
};

extern struct k_msgq adc_sample_q;  // depth 20, roughly 200 ms of headroom at 100 Hz
```

### ADC channel configuration (`boards/nrf5340dk_nrf5340_cpuapp.overlay`)

| Channel | Pin   | Gain | Reference  | Resolution | Range   |
|---------|-------|------|------------|------------|---------|
| AIN0    | P0.04 | 1/6  | Internal 0.6 V | 12-bit | 0 to 3.6 V |
| AIN1    | P0.05 | 1/6  | Internal 0.6 V | 12-bit | 0 to 3.6 V |
| AIN2    | P0.06 | 1/6  | Internal 0.6 V | 12-bit | 0 to 3.6 V |

Voltage conversion: `mv = raw * 3600 / 4096`

### BLE packet format (NUS TX notify)

Five samples are batched into one notify, sent every 50 ms (20 packets per second).

| Offset | Size | Field    | Type      | Description                          |
|--------|------|----------|-----------|--------------------------------------|
| 0      | 2    | seq      | uint16_t  | Rolling sequence number for gap detection |
| 2      | 1    | count    | uint8_t   | Samples in this packet (always 5)    |
| 3      | 4    | t0_us    | uint32_t  | Timestamp of the first sample (µs)   |
| 7      | 30   | raw[5][3]| int16_t[] | 5 samples × 3 channels, row-major    |
| **Total** | **37 bytes** | | | |

- As long as `struct adc_sample` in `adc_sampler.h` stays the same, the BLE side needs no changes.

## Building

**Requirements:** nRF Connect SDK v3.3.1, west

```bash
west build -b nrf5340dk/nrf5340/cpuapp --sysbuild --pristine
west flash
```

`--sysbuild` is required. BLE on the nRF5340 runs on the network core (cpunet), and sysbuild compiles and flashes both cores together.

### Viewing RTT logs

Open `C:\Program Files\SEGGER\JLink_V898\JLinkRTTViewer.exe` with these settings:

- Connection: USB
- Target Device: NRF5340_XXAA_APP
- Target Interface: SWD
- Speed: 4000 kHz
- RTT Control Block: Auto Detection

A healthy boot looks like:

```
[INF] ADC sampler ready, 3 channels
[INF] BT enabled
[INF] ADC sampling started @ 100 Hz
[INF] Advertising successfully started
```

## PC Logger Script

### Requirements

Python 3.10 or later.

```bash
pip install bleak
```

### Usage

```bash
cd tools

python ble_logger.py                      # auto-named CSV with timestamp
python ble_logger.py -o my_data.csv       # custom output file
python ble_logger.py --name Device_ADC    # if the device name was changed
```

The script scans, connects, and streams to CSV until Ctrl+C.

### CSV columns

| Column      | Description                                      |
|-------------|--------------------------------------------------|
| packet_seq  | BLE packet sequence number (use for gap detection) |
| sample_idx  | Index within the packet (0 to 4)                 |
| t0_us       | Firmware timestamp of the first sample in the packet (µs) |
| ch0/1/2_raw | Raw ADC value per channel (int16)                |
| ch0/1/2_mv  | Converted voltage per channel (mV, 2 decimal places) |

To reconstruct the exact timestamp for each sample: `timestamp_us = t0_us + sample_idx * 10000`
