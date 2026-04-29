# IEEE 802.15.4 Protobuf Receiver App

This is a minimal ESP32 receiver application that:

1. Initializes IEEE 802.15.4 radio on channel 15
2. Listens for incoming frames
3. Decodes protobuf-encoded KeyPress messages
4. Logs received data to serial output

## Building

```bash
idf.py build
```

## Flashing

```bash
idf.py flash
idf.py monitor
```

## Configuration

Edit `main.c` and change `IEEE802154_CHANNEL` to match your sender's channel (default: 15).

## Expected Output

```
IEEE 802.15.4 Protobuf Receiver
========================================
IEEE 802.15.4 receiver ready on channel 15
Receiver initialized. Listening on channel 15...
Waiting for messages...

Frame RX: ver=1 type=0x01 seq=1 dev=1 payload_len=6 rssi=-50
KEY_PRESS: key_id=2 state=PRESSED ts=1234 ms
```

## Message Types

- **0x01 (KEY_PRESS)**: Button press event with protobuf KeyPress
  - key_id: Button identifier
  - state: 1=PRESSED, 0=RELEASED
  - ts: Timestamp in milliseconds

- **0x02 (LIGHT_STATE)**: Light state update
- **0x03 (ACK)**: Acknowledgment
- **0x04 (HEARTBEAT)**: Periodic heartbeat
