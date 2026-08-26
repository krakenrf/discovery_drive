# Discovery Drive Firmware

Firmware for the Discovery Drive rotator

For the ESP32-S3. (Select ESP32S3 Dev Module in Arduno IDE)

Enable USB-CDC On Boot, and USB Mode: USB-OTG (TinyUSB) to enable serial over USB

Partition Scheme - Minimal SPIFFS 19.MB APP with OTA/190kB SPIFFS

## EMC test mode

For EMC compliance testing, browse to `http://<device>/enterEmcMode` (e.g.
`http://discoverydrive.local/enterEmcMode`). The device reboots into a minimal
test interface (motors disabled) from which a continuous CW carrier can be
started on a chosen 2.4 GHz channel with 0-20 dB TX attenuation. The carrier
transmits until power cycle; the "Clear EMC Test Mode Flag" button followed by
Reboot returns the device to the normal interface. There is deliberately no
button for this in the main UI.

## Required library: EMC_RFTest

The EMC test mode (CW carrier for EMC compliance testing) links against
Espressif's precompiled RF certification test library. Before building, copy
`arduino_libraries/EMC_RFTest` into your Arduino sketchbook libraries folder
(e.g. `Documents/Arduino/libraries/EMC_RFTest`).

The bundled `librftest.a` matches arduino-esp32 core 3.3.5 (ESP-IDF v5.5). If
the core is updated, re-fetch it from the `esp_phy` submodule commit of the
matching ESP-IDF revision (see `arduino_libraries/EMC_RFTest/src/EMC_RFTest.h`).
