# MS2 CAN bench simulator

Second ESP32 (any S3 or C3 with a TWAI transceiver) that pretends to be a
MegaSquirt MS2 with Dash Broadcasting on. Wire CAN-H/CAN-L to the gauge's
transceiver with 120 ohm at each end.

    . ..\..\tools\env.ps1
    idf.py set-target esp32s3
    idf.py menuconfig      # MS2 CAN simulator -> pins (default TX 16, RX 17)
    idf.py -p COMx flash monitor
