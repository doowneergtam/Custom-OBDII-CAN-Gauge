# MegaSquirt dash broadcast (simplified set)

Source: "Megasquirt-2 and Megasquirt-3 CAN realtime data broadcasting
format", dated 2016-02-17, https://www.msextra.com/doc/pdf/Megasquirt_CAN_Broadcast.pdf

- Supported on MS2 / Microsquirt / MSPNP2 with **MS2/Extra 3.4.x**, and MS3 with 1.4.x.
- 11-bit identifiers, **500 kbps**, sequential IDs from base **1512 (0x5E8)**. **Big-endian**.
- TunerStudio: CAN-Bus/Testmodes -> CAN Parameters -> Master enable; then
  Dash Broadcasting -> Enable On. "Automatic" locks the base ID to 1512 and
  sends at **20 Hz on MS2** (50 Hz on MS3). "Advanced" lets ID and rate change.
- Value = (raw * Multiply / Divide) + Add. All Multiply = 1, Add = 0.

| ID | Byte | Size | Signed | Name | Meaning | Units | Divide | On MS2 |
|---|---|---|---|---|---|---|---|---|
| 1512 | 0 | 2 | Y | map | Manifold pressure | kPa | 10 | Y |
| 1512 | 2 | 2 | - | rpm | Engine speed | RPM | 1 | Y |
| 1512 | 4 | 2 | Y | clt | Coolant temp | deg F | 10 | Y |
| 1512 | 6 | 2 | Y | tps | Throttle position | % | 10 | Y |
| 1513 | 0 | 2 | - | pw1 | Pulse width bank 1 | ms | 1000 | Y |
| 1513 | 2 | 2 | - | pw2 | Pulse width bank 2 | ms | 1000 | Y |
| 1513 | 4 | 2 | Y | mat | Manifold air temp | deg F | 10 | Y |
| 1513 | 6 | 2 | Y | adv_deg | Spark advance | deg BTDC | 10 | Y |
| 1514 | 0 | 1 | - | afrtgt1 | AFR target bank 1 | AFR | 10 | Y |
| 1514 | 1 | 1 | - | AFR1 | AFR cyl 1 | AFR | 10 | (Y) |
| 1514 | 2 | 2 | Y | EGOcor1 | EGO correction cyl 1 | % | 10 | (Y) |
| 1514 | 4 | 2 | Y | egt1 | EGT 1 | deg F | 10 | - |
| 1514 | 6 | 2 | Y | pwseq1 | Sequential PW cyl 1 | ms | 1000 | Y |
| 1515 | 0 | 2 | Y | batt | Battery voltage | V | 10 | Y |
| 1515 | 2 | 2 | Y | sensors1 | Generic sensor 1 (gpioadc0) | - | 10 | Y |
| 1515 | 4 | 2 | Y | sensors2 | Generic sensor 2 (gpioadc1) | - | 10 | Y |
| 1515 | 6 | 1 | - | knk_rtd | Knock retard | deg | 10 | Y |
| 1516 | 0 | 2 | - | VSS1 | Vehicle speed | m/s | 10 | - |
| 1516 | 2 | 2 | Y | tc_retard | Traction control retard | deg | 10 | - |
| 1516 | 4 | 2 | Y | launch_timing | Launch timing | deg | 10 | - |
| 1516 | 6 | 2 | - | - | not used | - | - | - |

Notes from the spec: EGT, VSS and knock may need extra hardware; MS2 ECUs
send a subset ("(Y)" = depends on configuration). Fields marked "-" on MS2 may
still be transmitted as zero. VSS1 on MS2 requires the VSS wired to a spare
digital input and enabled in Advanced settings; the 29-bit protocol doc says
MS2 does support VSS in 3.4.x, so treat the "-" as "unverified" until seen on
the bench.

The generic sensors (1515 bytes 2 and 4) are where oil pressure and oil temp
land. Their units depend on how the MS2 generic sensor is calibrated in
TunerStudio, so the gauge preset has to let the user set units and range for
those two channels.

## Channel preset derived from this table

Scale is the gauge's `scale` field (value = raw * scale + offset).

| Channel | ID | start bit | len | signed | scale | offset | units |
|---|---|---|---|---|---|---|---|
| MAP | 0x5E8 | 0 | 16 | Y | 0.1 | 0 | kPa |
| RPM | 0x5E8 | 16 | 16 | N | 1 | 0 | rpm |
| CLT | 0x5E8 | 32 | 16 | Y | 0.1 | 0 | F (convert to C in display layer if units=C) |
| TPS | 0x5E8 | 48 | 16 | Y | 0.1 | 0 | % |
| PW1 | 0x5E9 | 0 | 16 | N | 0.001 | 0 | ms |
| MAT | 0x5E9 | 32 | 16 | Y | 0.1 | 0 | F |
| ADV | 0x5E9 | 48 | 16 | Y | 0.1 | 0 | deg |
| AFRTGT | 0x5EA | 0 | 8 | N | 0.1 | 0 | AFR |
| AFR | 0x5EA | 8 | 8 | N | 0.1 | 0 | AFR |
| EGOCOR | 0x5EA | 16 | 16 | Y | 0.1 | 0 | % |
| BATT | 0x5EB | 0 | 16 | Y | 0.1 | 0 | V |
| OILP (sensors1) | 0x5EB | 16 | 16 | Y | 0.1 | 0 | psi (user) |
| OILT (sensors2) | 0x5EB | 32 | 16 | Y | 0.1 | 0 | F (user) |
| KNOCK | 0x5EB | 48 | 8 | N | 0.1 | 0 | deg |
| VSS | 0x5EC | 0 | 16 | N | 0.1 | 0 | m/s (display converts to mph/kph) |

Bit numbering is MSB-first within the big-endian frame: start bit 0 is the
MSB of data byte 0.
