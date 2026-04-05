## Project Overview

This project runs on an ESP32-family device and streams IMU data (ICM-42688-P) over UDP using JSON messages. The data is broadcast on the local network and intended to be consumed by a Tauri-based desktop frontend.

Two output paths are provided:

- **Serial output** (USB) for debugging and development
- **UDP broadcast** for real-time data ingestion by the Tauri backend

---

## Data Format

The ESP32 sends JSON-formatted IMU samples at **1 Hz**.

### Example UDP / Serial Packet

```json
{
  "type": "IMU",
  "data": {
    "timestamp_us": 123456789,
    "ax": 0.012,
    "ay": -0.981,
    "az": 0.034,
    "gx": 0.002,
    "gy": -0.015,
    "gz": 0.001
  }
}
```

- `timestamp_us`: Microsecond timestamp from the ESP32
- `ax, ay, az`: Accelerometer values
- `gx, gy, gz`: Gyroscope values

---

## Network Behavior

- The ESP32 connects to Wi-Fi in **station mode**
- After obtaining an IP address, it computes the subnet broadcast address (`x.x.x.255`)
- IMU data is broadcast via UDP to the configured port

This allows multiple listeners (including the Tauri backend) to receive data without maintaining a persistent connection.

---

## Configuration

Configure the project using:

```bash
idf.py menuconfig
```

### Required Settings

1. **Wi-Fi Credentials**
   Path:
   `Project Configuration → WiFi Configuration`

   - Set SSID
   - Set password

2. **UDP Broadcast Port**
   Path:
   `Project Configuration → Broadcast Configuration`

   - `CONFIG_BROADCAST_PORT`
   - Must match the port used by the Tauri backend

3. **Console Output Channel**
   Path:
   `Component config → ESP System Settings`

   - Set **Channel for console output** to
     `USB Serial/JTAG Controller`

---

## Build and Flash

```bash
idf.py build
idf.py flash
idf.py monitor
```

⚠ **Important:**
The serial monitor must be stopped before running the Tauri backend, otherwise the USB serial port may be locked.

---

## Tauri Backend Integration

- The Tauri backend should:

  - Bind to `0.0.0.0:<CONFIG_BROADCAST_PORT>`
  - Listen for UDP packets
  - Parse incoming JSON messages

- No handshake or connection setup is required
- Multiple ESP32 devices can broadcast simultaneously

---

## Supported Targets

| Supported Targets | ESP32 | ESP32-C2 | ESP32-C3 | ESP32-C5 | ESP32-C6 | ESP32-C61 | ESP32-H2 | ESP32-P4 | ESP32-S2 | ESP32-S3 |
| ----------------- | ----- | -------- | -------- | -------- | -------- | --------- | -------- | -------- | -------- | -------- |

---

## Notes

- UDP is connectionless and packets may be dropped under heavy network load
- This design favors low latency over guaranteed delivery
- Increase task stack size or reduce JSON size if additional sensors are added
