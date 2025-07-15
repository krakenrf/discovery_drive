/*
 * Firmware for the discovery-drive satellite dish rotator.
 * Stellarium Poller - Poll the stellarium web data interface and send
 * control information to the motors.
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

#include "stellarium_poller.h"

// =============================================================================
// CONSTRUCTOR AND INITIALIZATION
// =============================================================================

StellariumPoller::StellariumPoller(Preferences& prefs, MotorSensorController& motorSensorCtrl, Logger& logger)
    : _preferences(prefs), _motorSensorCtrl(motorSensorCtrl), _logger(logger) {
}

void StellariumPoller::begin() {
    // Initialize any required resources here
    _logger.info("StellariumPoller initialized");
}

// =============================================================================
// CORE FUNCTIONALITY
// =============================================================================

void StellariumPoller::runStellariumLoop(bool serialActive, String rotctl_client_ip, bool wifiConnected) {
    // Check if Stellarium polling should be active
    if (!shouldPollStellarium(serialActive, rotctl_client_ip)) {
        setStellariumConnActive(false);
        return;
    }

    if (!wifiConnected) {
        _logger.error("WiFi Disconnected");
        setStellariumConnActive(false);
        return;
    }

    // Attempt to poll Stellarium data
    if (pollStellariumData()) {
        setStellariumConnActive(true);
    } else {
        setStellariumConnActive(false);
    }
}

bool StellariumPoller::shouldPollStellarium(bool serialActive, String rotctl_client_ip) {
    // Only poll if Stellarium is enabled and no other connections are active
    return (getStellariumOn() && 
            !serialActive && 
            rotctl_client_ip == "NO ROTCTL CONNECTION");
}

bool StellariumPoller::pollStellariumData() {
    HTTPClient http;
    
    String stellariumServerIP = _preferences.getString("stelServIP", "NO IP SET");
    String stellariumServerPort = _preferences.getString("stelServPort", "8090");
    String stellariumURL = "http://" + stellariumServerIP + ":" + stellariumServerPort + "/api/objects/info";

    if (!http.begin(stellariumURL)) {
        _logger.error("HTTP begin failed for: " + stellariumURL);
        return false;
    }

    // Configure HTTP client
    http.setConnectTimeout(5000);
    
    int httpResponseCode = http.GET();
    bool success = false;

    if (httpResponseCode > 0) {
        String payload = http.getString();
        success = processApiResponse(payload);
    } else {
        _logger.error("HTTP request failed with code: " + String(httpResponseCode));
    }

    http.end();
    return success;
}

bool StellariumPoller::processApiResponse(const String& payload) {
    // Extract Azimuth and Altitude from payload
    String azAltStr = getValue(payload, "Az./Alt.: ", " ");
    
    if (azAltStr == "") {
        _logger.info("No Az./Alt. data found in Stellarium response");
        return false;
    }

    // Parse azimuth and elevation values
    int separatorIndex = azAltStr.indexOf('/');
    if (separatorIndex == -1) {
        _logger.error("Invalid Az./Alt. format: " + azAltStr);
        return false;
    }

    float az = (float)parseDMS(azAltStr.substring(0, separatorIndex));
    float el = (float)parseDMS(azAltStr.substring(separatorIndex + 1));

    // Validate and clean up azimuth
    if (isnan(az)) az = 0;
    az = fmod(az, 360.0);
    if (az < 0) {
        az += 360.0;
    }

    // Validate and clean up elevation
    if (isnan(el)) el = 0;
    if (el < 0) el = 0;
    if (el > 90) el = 90;

    // Update motor controller setpoints
    _motorSensorCtrl.setSetPointAz(az);
    _motorSensorCtrl.setSetPointEl(el);

    _logger.info("Stellarium target - Az: " + String(az, 2) + "°, El: " + String(el, 2) + "°");
    
    return true;
}

// =============================================================================
// UTILITY METHODS
// =============================================================================

String StellariumPoller::getValue(String data, String start, String end) {
    int startIndex = data.indexOf(start);
    if (startIndex == -1) return "";
    
    startIndex += start.length();
    int endIndex = data.indexOf(end, startIndex);
    if (endIndex == -1) return "";
    
    return data.substring(startIndex, endIndex);
}

double StellariumPoller::parseDMS(String dms) {
    dms.trim();
    
    // Find degree, minute, and second markers
    int degIndex = dms.indexOf("\u00b0");
    int minIndex = dms.indexOf("'", degIndex);
    int secIndex = dms.indexOf("\"", minIndex);

    // Validate format
    if (degIndex == -1 || minIndex == -1 || secIndex == -1) {
        _logger.warn("Invalid DMS format: " + dms);
        return 0.0;
    }

    // Extract individual components
    double degrees = dms.substring(0, degIndex).toDouble();
    double minutes = dms.substring(degIndex + 2, minIndex).toDouble();
    double seconds = dms.substring(minIndex + 1, secIndex).toDouble();

    // Convert to decimal degrees
    double decimalDegrees = abs(degrees) + (minutes / 60.0) + (seconds / 3600.0);
    
    // Apply sign if negative
    if (dms.startsWith("-")) {
        decimalDegrees = -decimalDegrees;
    }

    return decimalDegrees;
}

// =============================================================================
// GETTER AND SETTER METHODS
// =============================================================================

bool StellariumPoller::getStellariumConnActive() {
    return _stellariumConnActive;
}

void StellariumPoller::setStellariumConnActive(bool on) {
    _stellariumConnActive = on;
}

bool StellariumPoller::getStellariumOn() {
    return _stellariumOn;
}

void StellariumPoller::setStellariumOn(bool on) {
    _stellariumOn = on;
    _logger.info("Stellarium polling " + String(on ? "enabled" : "disabled"));
}