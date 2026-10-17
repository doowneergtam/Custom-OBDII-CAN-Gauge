# Pod gauge pin map — Waveshare ESP32-S3-Touch-AMOLED-1.75

Source: Waveshare `HARDWARE_REFERENCE.md` in the board repo
(https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75), read 2026-10-10.
This supersedes the GPIO 1/2/3 guess in the project brief: GPIO1-3 are the
SD card pins. The header carries GPIO16, 17, 18 and UART0 (GPIO43/44).

## Onboard (fixed by the board, handled by the BSP)

| Block | Signal | GPIO |
|---|---|---|
| CO5300 AMOLED, QSPI | SIO0..3 | 4, 5, 6, 7 |
| | PCLK | 38 |
| | CS | 12 |
| | RST | 39 |
| | TE | 13 |
| CST9217 touch | INT | 11 |
| | RST | 40 |
| Shared I2C | SCL / SDA | 14 / 15 |
| I2S audio | MCLK / BCLK / LRCK | 42 / 9 / 45 |
| | DOUT (ES8311) / DIN (ES7210) | 8 / 10 |
| | PA enable | 46 |
| SD card, 1-bit SDMMC | CMD / CLK / D0 / D3 | 1 / 2 / 3 / 41 |
| Native USB | D- / D+ | 19 / 20 |
| IMU | QMI_INT2 | 21 |
| Boot button | BOOT | 0 |

I2C addresses (7-bit): ES8311 0x18, TCA9554 0x20, AXP2101 0x34, ES7210 0x40,
PCF85063 0x51, CST9217 0x5A, QMI8658 0x6B.

TCA9554 EXIO: 0-2 free (test points), 3 RTC INT, 4 PWR button, 5 AXP2101 INT,
6 QMI8658 INT1, 7 LC76G reset (-G variant only).

## Expansion header H2 (8-pin, 2.54 mm)

Rev C carrier map (2026-10-16). Pin 1 is the IO18 end when looking at the
back of the Waveshare with the header at the bottom. The encoder is gone:
touch does everything it did, and its two ADC-capable pins became the analog
inputs.

| Pin | Board signal | Gauge use (carrier net) |
|---|---|---|
| 1 | GPIO18 (ADC2 ch 7) | Analog input 2 (AIN2) |
| 2 | GPIO17 (ADC2 ch 6) | Analog input 1 (AIN1) |
| 3 | GPIO16 | unused |
| 4 | GPIO44 (U0RXD) | CAN TX to the WCMCU-230 CTX (CAN_TX) |
| 5 | GPIO43 (U0TXD) | CAN RX from the WCMCU-230 CRX through R3 1 kΩ (CAN_RX) |
| 6 | 3V3 | 3V3 to the WCMCU-230 |
| 7 | GND | GND |
| 8 | VBUS (5 V in) | 5 V from the Traco TSR 1-2450 |

Firmware must not enable the knob driver on 17/18 any more: its pull-ups
would load the sensor dividers. ADC2 is shared with the radio, so reads are
retried when the hotspot is up.

### Carrier connectors (rev C)

| J2 HY2.0-5P, car | J3 HY2.0-4P, sensors |
|---|---|
| 1 GND | 1 5V out (for 0-5 V senders) |
| 2 LIGHTS (12 V when lights on) | 2 GND |
| 3 CAN L | 3 Sensor 1 (to AIN1 via 1k / 1k5 divider) |
| 4 CAN H | 4 Sensor 2 (to AIN2 via 1k / 1k5 divider) |
| 5 12 V ignition | |

Per input: sensor → 1 kΩ → GPIO, 1.5 kΩ GPIO → GND (0-5 V scales to 0-3 V).
Solder bridge SJ1/SJ2 closed adds a 1 kΩ pull-up from 5 V at the sensor node
for resistive (temperature) senders; open for 0-5 V senders.

### Why CAN sits on the UART0 pins now

The rev A plan put CAN on 16/17 to keep the ROM boot log, which the chip
prints on GPIO43 (U0TXD) for a few hundred milliseconds at every reset, off
the CAN transceiver. On the rev B carrier the CAN module's CTX/CRX pins land
directly above header pins 4 and 5, which makes the single-sided board
routable without wire links, so the roles moved:

- CAN TX is GPIO44 (U0RXD). During boot it is an input with a pull-up, so the
  transceiver TXD sees a steady high, which is CAN recessive. Nothing is ever
  put on the bus before our firmware starts the TWAI driver.
- CAN RX is GPIO43 (U0TXD). The ROM drives it as an output while it prints,
  and the transceiver's RXD output drives it too; R3 (1 kΩ on the carrier)
  limits that contention to a few milliamps for the boot window. RX only
  listens, so the boot log cannot reach the bus.
- Encoder A/B/push are on 18/17/16, read through PCNT and the button
  component; any GPIO works there.

Consequences:
- Console and flashing stay on native USB (USB-Serial-JTAG,
  `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`), so nothing else drives 43/44.
- Firmware recovery over UART0 still works: unplug the CAN module first.
- Stock-ECU K-line fallback (L9637D) would need UART1 on 43/44, which now
  collides with CAN. If that fallback is ever built it goes on 16/17 and the
  encoder moves to TCA9554 EXIO0/1.

## Power

Feed 5 V into VBUS from an automotive 12 V to 5 V buck (36 V input rating,
TVS, reverse polarity) on ignition-switched 12 V. Battery connector stays
empty. The AXP2101 power path is between VBUS and the 3V3 rail, so do not
inject 3V3 from outside.
