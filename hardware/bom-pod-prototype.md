# Pod prototype parts list

What to order for build steps 1 to 5 plus the bench rig. Quantities are for
one gauge and one bench simulator.

| Qty | Part | Notes |
|---|---|---|
| 1 | Waveshare ESP32-S3-Touch-AMOLED-1.75 | Not the -G (GPS) variant; -G uses GPIO17/18 for the GPS UART. Not the 1.75C. |
| 1 | Rotary encoder with push, 20 to 24 detents, e.g. Bourns PEC11R-4215F-S0024 or Alps EC11 | Panel mount, D-shaft. Add a knob. |
| 1 | CAN transceiver breakout, 3.3 V: TJA1051T/3 or SN65HVD230 | TJA1051T/3 is the automotive-rated choice; SN65HVD230 boards are cheaper and fine on the bench. Avoid 5 V-only MCP2551/TJA1050 boards. |
| 2 | 120 ohm resistors | One per bus end. Many transceiver boards have a termination jumper. |
| 1 | Automotive 12 V to 5 V buck, 36 V input, 2 A | Look for load-dump / surge rated and reverse-polarity protection. Avoid USB cigarette-lighter adapters. |
| 1 | 8-pin 2.54 mm socket or JST-XH pigtail for H2 | Match the header on the Waveshare board. |
| 1 | Second ESP32-S3 dev board (any) + second CAN transceiver | Bench MS2 simulator (`tools/can_sim`). An ESP32-C3 SuperMini works too. |
| 1 | 52 mm gauge pod | A-pillar or dash top pod for a 2-1/16 inch gauge. The Waveshare board is 1.75 inch glass; a 3D-printed bezel adapts it. |
| 1 | USB-C cable with data | Flashing and console over native USB. |

Nice to have now:

| Qty | Part | Notes |
|---|---|---|
| 1 | Short JST SH 1.0 mm cables | The board's small connectors (battery, I2C pads) if ever used. |
| 1 | USB CAN adapter (CANable / candleLight) | Watch the bus from a PC during bench testing. |

Do not order yet: ADS1115, L9637D (stock-ECU fallback only), ESP32-P4 kit
(cluster project, decide after the display interface question in the brief).
