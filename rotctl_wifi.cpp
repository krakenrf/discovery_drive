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

#include "rotctl_wifi.h"

RotctlWifi::RotctlWifi(Preferences& prefs, MotorSensorController& motorSensorCtrl, Logger& logger)
    : _preferences(prefs), _motorSensorCtrl(motorSensorCtrl), _logger(logger) {
}

void RotctlWifi::begin() {
    _rotator_server = new WiFiServer(_preferences.getInt("rotctl_port", DEFAULT_ROTCTL_PORT));
    _rotator_server->begin();
    _logger.info("Rotator rotctl TCP server started");
}

void RotctlWifi::rotctlWifiLoop(bool serialActive, bool stellariumOn) {
    // If stellarium polling is on, or serial is active, disconnect rotctl client
    if (stellariumOn || serialActive) {
        if (_rotator_client && _rotator_client.connected()) {
            disconnectClient();
        }
        return;
    }
    
    // Handle rotator control for GPredict and Look4Sat direct control
    handleClientConnection();
    handleClientCommands();
    
    // Check for disconnected clients
    if (_rotator_client && !_rotator_client.connected()) {
        disconnectClient();
    }
}

void RotctlWifi::handleClientConnection() {
    if (!_rotator_client || !_rotator_client.connected()) {
        _rotator_client = _rotator_server->available();
        if (_rotator_client) {
            _logger.info("New client connected");
            _rotctl_client_ip = _rotator_client.remoteIP().toString();
            _rotator_client.setTimeout(60);
            _lastClientActivity = millis();
        }
    }
}

void RotctlWifi::handleClientCommands() {
    if (!_rotator_client || !_rotator_client.connected()) {
        _rotctl_client_ip = "NO ROTCTL CONNECTION";
        return;
    }
    
    if (_rotator_client.available() <= 0) {
        return;
    }
    
    String request = readCommandFromClient();
    if (request.length() == 0) {
        return;
    }
    
    _logger.info("Received message: " + request);
    
    // Handle different rotctl commands
    if (request.startsWith("\\P") || request.startsWith("P")) {
        handlePositionCommand(request);
    } else if (request == "p") {
        handleGetPositionCommand();
    } else if (request == "s") {
        handleStopCommand();
    } else if (request == "R") {
        handleResetCommand();
    } else {
        _logger.error("Unexpected message format: " + request);
    }
    
    _lastClientActivity = millis();
}

String RotctlWifi::readCommandFromClient() {
    String request = "";
    unsigned long startMillis = millis();
    
    // Manual command reading to avoid blocking on readStringUntil('\n')
    while (millis() - startMillis < READ_TIMEOUT) {
        char c;
        while (_rotator_client.available()) {
            c = _rotator_client.read();
            if (c == '\n') {
                return request;
            }
            request += c;
        }
        if (c == '\n') {
            break;
        }
    }
    
    return request;
}

void RotctlWifi::handlePositionCommand(const String& request) {
    float az, el;
    
    if (request.startsWith("\\P")) {
        sscanf(request.c_str(), "\\P %f %f", &az, &el);
    } else {
        sscanf(request.c_str(), "P %f %f", &az, &el);
    }
    
    az = cleanupAzimuth(az);
    el = cleanupElevation(el);
    
    _motorSensorCtrl.setSetPointAz(az);
    _motorSensorCtrl.setSetPointEl(el);
    
    _logger.info("Parsed Azimuth: " + String(_motorSensorCtrl.getSetPointAz(), 2) +
                 ", Elevation: " + String(_motorSensorCtrl.getSetPointEl(), 2));
    
    _rotator_client.print("RPRT 0\n");
}

void RotctlWifi::handleGetPositionCommand() {
    float el = _motorSensorCtrl.getCorrectedAngleEl();
    
    // Hack to stop it breaking displays in satdump when el sits at ~359.99 instead of 0
    if (el > 359) {
        el = 0;
    }
    
    String response = String(_motorSensorCtrl.getCorrectedAngleAz(), 2) + "\n" + 
                     String(el, 2) + "\n";
    _rotator_client.print(response);
    _logger.info("Responded with position: " + response);
}

void RotctlWifi::handleStopCommand() {
    _motorSensorCtrl.setSetPointAz(_motorSensorCtrl.getCorrectedAngleAz());
    _motorSensorCtrl.setSetPointEl(_motorSensorCtrl.getCorrectedAngleEl());
}

void RotctlWifi::handleResetCommand() {
    _motorSensorCtrl.setSetPointAz(0);
    _motorSensorCtrl.setSetPointEl(0);
}

float RotctlWifi::cleanupAzimuth(float az) {
    if (isnan(az)) {
        az = 0;
    }
    
    az = fmod(az, 360.0);
    if (az < 0) {
        az += 360.0;
    }
    
    return az;
}

float RotctlWifi::cleanupElevation(float el) {
    if (isnan(el)) {
        el = 0;
    }
    
    if (el < 0) el = 0;
    if (el > 90) el = 90;
    
    return el;
}

void RotctlWifi::disconnectClient() {
    _rotctl_client_ip = "NO ROTCTL CONNECTION";
    _rotator_client.stop();
    _rotator_client = WiFiClient();
}

String RotctlWifi::getRotctlClientIP() {
    return _rotctl_client_ip;
}

bool RotctlWifi::isRotctlConnected() {
    return _rotator_client.connected();
}