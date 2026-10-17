# JK BMS UART protocol (`4E 57`)

Source: [syssi/esphome-jk-bms](https://github.com/syssi/esphome-jk-bms)
(`docs/protocol-design-uart-ttl-gps-port.md`, `components/jk_modbus`, `components/jk_bms`).
Verified in this project with a 20S JK, HW 11.XW / SW 11.272.

## Physical layer

- 4-pin JST 1.25 mm socket: `GND – RX – TX – VBAT` (labelled "GPS" or "RS485" on some models)
- **3.3 V TTL**, **115200 baud, 8N1**
- VBAT = pack voltage (meant to power a GPS module); leave it unconnected.
- The JK only answers when asked (slave).

## "Read all data" request (21 bytes)

```
4E 57 00 13 00 00 00 00 06 03 00 00 00 00 00 00 68 00 00 01 29
```

| Byte | Value | Meaning |
|---|---|---|
| 0–1 | `4E 57` | Start ("NW") |
| 2–3 | `00 13` | Length = total bytes − 2 = 19 |
| 4–7 | `00 00 00 00` | Terminal number |
| 8 | `06` | Command: read all data |
| 9 | `03` | Source: 00 BMS, 01 Bluetooth, 02 GPS, 03 PC |
| 10 | `00` | Type: 00 request, 01 response, 02 active upload |
| 11 | `00` | Data ID: 00 = all |
| 12–15 | `00 00 00 00` | Record number |
| 16 | `68` | End |
| 17–20 | `00 00 01 29` | Checksum: first 2 bytes 0, last 2 bytes = sum of bytes 0…16 |

With source `02`: `… 06 02 … 68 00 00 01 28`. In this project the JK answered to `03`.

Other commands: `01` activate, `02` write, `03` read a single register, `05` password.
Writing settings takes a password frame followed by a write frame; this project only reads.

## Response

The response starts with `4E 57` and ends with `68 00 00 xx xx`. It is ~280–340 bytes long, depending on the cell count.

```
4E 57 LL LL 00 00 00 00 06 00 01 | data: ID value ID value … | 00 00 00 00 68 00 00 CS CS
```

- Checksum: 16-bit sum of all bytes from the start up to the last 2 bytes = the last 2 bytes.
- The data area (from byte 11 up to the record number) is made of **ID + value** pairs.

### Fields used

| ID | Bytes | Field | Decoding |
|---|---|---|---|
| `79` | 1 + 3n | Cell voltages | `79 <length>`, then `no, mV_H, mV_L` per cell. 20 cells → `79 3C` |
| `80` | 2 | MOSFET temperature | 0–100 = °C, 101–140 = −1…−40 °C |
| `81` | 2 | Temperature T1 | same |
| `82` | 2 | Temperature T2 | same |
| `83` | 2 | Total voltage | 0.01 V |
| `84` | 2 | Current | Protocol 1: bit15 = 1 charge / 0 discharge, remaining 15 bits in 0.01 A. Protocol 0: `(10000 − value) × 0.01 A` |
| `85` | 1 | SOC | % |
| `86` | 1 | Number of temperature sensors | |
| `87` | 2 | Cycle count | |
| `89` | 4 | Total cycle capacity | Ah |
| `8A` | 2 | Cell count | |
| `8B` | 2 | Alarms | see below |
| `8C` | 2 | Status | bit0 charge MOS, bit1 discharge MOS, bit2 balancing |
| `C0` | 1 | Protocol version | selects the `84` current format |

The other IDs (8E…BA, settings and text fields) have fixed lengths; the decoder simply skips over them
(`jk_bms.h` → `fieldLen()`).

Real example (from the esphome-jk-bms docs, 14 cells):
`83 14 EF` = 53.59 V, `84 80 D0` = +2.08 A (charging), `85 0F` = 15 %, `C0 01` = protocol 1.

### Alarm bits (`8B`)

| Bit | Alarm |
|---|---|
| 0 | Low capacity |
| 1 | MOSFET over-temperature |
| 2 | Charge over-voltage |
| 3 | Discharge under-voltage |
| 4 | Battery over-temperature |
| 5 | Charge over-current |
| 6 | Discharge over-current |
| 7 | Cell voltage difference |
| 8 | Box over-temperature |
| 9 | Battery under-temperature |
| 10 | Cell over-voltage |
| 11 | Cell under-voltage |

## Testing from a PC

With a USB-TTL adapter (or a Nano with its RST pin tied to GND), send the 21 bytes above at 115200 from a HEX
terminal such as Hercules or RealTerm. Readable text shows up in the reply: the password (`123456`),
manufacturing date, firmware version (e.g. `11.XW_S11.272`), manufacturer record.
