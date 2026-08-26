/*
 * Firmware for the discovery-drive satellite dish rotator.
 * EMC test mode - Minimal boot mode for EMC compliance testing. Serves a
 * standalone test page from which a continuous CW carrier can be emitted
 * on a chosen WiFi channel using Espressif's RF certification test API.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef EMC_TEST_H
#define EMC_TEST_H

// Arduino/System includes
#include <Arduino.h>
#include <WebServer.h>
#include <Preferences.h>

// Espressif RF certification test API. The implementation is the precompiled
// librftest.a provided by the EMC_RFTest library (see arduino_libraries/),
// which must match the PHY libraries of the installed arduino-esp32 core.
#include <EMC_RFTest.h>

// Custom includes
#include "wifi_manager.h"
#include "logger.h"

class EmcTestMode {
public:
    EmcTestMode(Preferences& prefs, WiFiManager& wifiManager, Logger& logger);

    // True when the persistent EMC test mode flag ("emc_mode") is set
    bool isEnabled();

    // Boot into EMC test mode. Never returns.
    // Either transmits a CW carrier (one-shot "emc_cw" flag, cleared on read
    // so any reboot stops the carrier) or serves the EMC test web page.
    void run();

private:
    Preferences& _preferences;
    WiFiManager& _wifiManager;
    Logger& _logger;

    WebServer* _server = nullptr;

    // Authentication (shared with the normal web UI credentials)
    char _loginUser[33] = "";
    char _loginPassword[65] = "";

    void parkMotorPins();
    bool checkAuth();
    void setupRoutes();
    void runCwTransmitForever();
    void runWebUiForever();

    static void WebTask(void* param);
};

#endif // EMC_TEST_H
