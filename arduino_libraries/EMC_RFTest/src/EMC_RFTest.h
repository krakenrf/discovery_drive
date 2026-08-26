/*
 * EMC_RFTest - Espressif RF certification test API for EMC compliance testing.
 *
 * The API declarations come from the arduino-esp32 core (esp_phy_cert_test.h).
 * The implementation is the precompiled src/esp32s3/librftest.a (plus its
 * dependency libbttestmode.a, needed by rftest_init), fetched from
 * https://github.com/espressif/esp-phy-lib @ cb9f62b42096ec1e20c05f2aa57fdd0d04df4d33
 * (the exact esp_phy commit used by arduino-esp32 3.3.5 / ESP-IDF v5.5).
 *
 * IMPORTANT: these .a files must match the PHY libraries of the installed
 * arduino-esp32 core. When updating the core, re-fetch them from the
 * esp_phy submodule commit of the matching ESP-IDF revision.
 */

#pragma once

#include "esp_phy_cert_test.h"
