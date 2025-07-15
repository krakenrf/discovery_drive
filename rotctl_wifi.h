/*
 * Firmware for the discovery-drive satellite dish rotator.
 * rotctl WiFi - Allow for direct rotctl connections over WiFi.
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

#ifndef ROTCTL_WIFI_H
#define ROTCTL_WIFI_H

#include <Arduino.h>
#include <Preferences.h>
#include <WiFiServer.h>
#include <WiFiClient.h>

#include "motor_controller.h"
#include "logger.h"

class RotctlWifi {
public:
    RotctlWifi(Preferences& prefs, MotorSensorController& motorController, Logger& logger);
    
    void begin();
    void rotctlWifiLoop(bool serialActive, bool stellariumOn);
    
    String getRotctlClientIP();
    bool isRotctlConnected();

private:
    static const unsigned long CLIENT_TIMEOUT = 10000;  // 10 seconds timeout
    static const unsigned long READ_TIMEOUT = 1000;     // 1 second read timeout
    static const int DEFAULT_ROTCTL_PORT = 4533;
    
    void handleClientConnection();
    void handleClientCommands();
    void handlePositionCommand(const String& request);
    void handleGetPositionCommand();
    void handleStopCommand();
    void handleResetCommand();
    void disconnectClient();
    String readCommandFromClient();
    float cleanupAzimuth(float az);
    float cleanupElevation(float el);
    
    Logger& _logger;
    MotorSensorController& _motorSensorCtrl;
    Preferences& _preferences;
    
    WiFiServer* _rotator_server;
    WiFiClient _rotator_client;
    String _rotctl_client_ip = "NO ROTCTL CONNECTION";
    unsigned long _lastClientActivity = 0;
};

#endif