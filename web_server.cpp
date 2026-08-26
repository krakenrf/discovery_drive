/*
 * Firmware for the discovery-drive satellite dish rotator.
 * Web Server - Creates a browser based UI for the discovery-drive.
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
 
#include "web_server.h"

// Rounding helpers — control JSON precision without String heap allocations
static inline float r2(float v) { return roundf(v * 100.0f) / 100.0f; }
static inline float r1(float v) { return roundf(v * 10.0f) / 10.0f; }
static inline float r3(float v) { return roundf(v * 1000.0f) / 1000.0f; }

// =============================================================================
// CONSTRUCTOR AND INITIALIZATION
// =============================================================================

WebServerManager::WebServerManager(Preferences& prefs, MotorSensorController& motorController, INA219Manager& ina219Manager, 
                StellariumPoller& stellariumPoller, WeatherPoller& weatherPoller, SerialManager& serialManager, 
                WiFiManager& wifiManager, RotctlWifi& rotctlWifi, Logger& logger)
    : preferences(prefs), msc(motorController), ina219Manager(ina219Manager), stellariumPoller(stellariumPoller),
      weatherPoller(weatherPoller), serialManager(serialManager), wifiManager(wifiManager), rotctlWifi(rotctlWifi), _logger(logger) {
    
    _fileMutex = xSemaphoreCreateMutex();
    _loginUserMutex = xSemaphoreCreateMutex();
}

void WebServerManager::begin() {
    server = new WebServer(preferences.getInt("http_port", 80));

    safeCopy(wifi_ssid, preferences.getString("wifi_ssid", "").c_str(), sizeof(wifi_ssid));
    safeCopy(wifi_password, preferences.getString("wifi_password", "").c_str(), sizeof(wifi_password));
    safeCopy(_loginUser, preferences.getString("loginUser", "").c_str(), sizeof(_loginUser));
    safeCopy(_loginPassword, preferences.getString("loginPassword", "").c_str(), sizeof(_loginPassword));

    setupRoutes();

    server->begin();
    _logger.info("HTTP server started");
}

// =============================================================================
// ROUTE SETUP
// =============================================================================

void WebServerManager::setupRoutes() {
    // Static file routes
    setupStaticRoutes();
    
    // Main page and OTA routes
    setupMainPageRoutes();
    
    // System control routes
    setupSystemControlRoutes();
    
    // Motor control routes  
    setupMotorControlRoutes();
    
    // Configuration routes
    setupConfigurationRoutes();
    
    // API and data routes
    setupAPIRoutes();
    
    // Debug and error handling
    setupDebugRoutes();
    
    _logger.debug("All routes registered");
}

void WebServerManager::setupStaticRoutes() {
    // Cache static assets for 1 hour — on page refresh, the browser serves
    // these from cache instead of hitting the ESP32, which can only handle
    // one request per handleClient() tick.
    server->on("/styles.css", HTTP_GET, [this]() {
        server->sendHeader("Cache-Control", "max-age=3600");
        handleStaticFile("/styles.css", "text/css");
    });

    server->on("/script.js", HTTP_GET, [this]() {
        server->sendHeader("Cache-Control", "max-age=3600");
        handleStaticFile("/script.js", "application/javascript");
    });

    server->on("/Logo-Circle-Cream.png", HTTP_GET, [this]() {
        server->sendHeader("Cache-Control", "max-age=86400");
        handleStaticFile("/Logo-Circle-Cream.png", "image/png");
    });
}

void WebServerManager::setupMainPageRoutes() {
    // Main page route with authentication
    server->on("/", HTTP_GET, [this]() {
        const char* loginUser = getLoginUser();
        const char* loginPassword = getLoginPassword();

        if (_loginRequired && loginUser[0] != '\0' && loginPassword[0] != '\0') {
            if (!server->authenticate(loginUser, loginPassword)) {
                return server->requestAuthentication();
            }
        }

        // Stream HTML directly from LittleFS — no ~40KB heap allocation.
        // Checkbox states are synced by JavaScript via /config and /status.
        handleStaticFile("/index.html", "text/html");
    });

    // OTA Update routes
    server->on("/ota", HTTP_GET, [this]() {
        handleOTAUpload();
    });

    setupFileUploadRoute();
    setupFirmwareUploadRoute();
}

void WebServerManager::setupSystemControlRoutes() {
    server->on("/restart", HTTP_POST, [this]() {
        String htmlResponse = createRestartResponse("Restarting", "Restarting...");
        server->send(200, "text/html", htmlResponse);
        delay(1000);
        ESP.restart();
    });

    server->on("/resetNeedsUnwind", HTTP_POST, [this]() {
        String htmlResponse = createRestartResponse("Restarting", "Restarting...");
        server->send(200, "text/html", htmlResponse);
        preferences.putInt("needs_unwind", 0);
        delay(1000);
        ESP.restart();
    });

    // Deliberately not exposed in the UI — EMC test labs enter test mode by
    // visiting this URL directly in a browser (hence HTTP_ANY, not just POST)
    server->on("/enterEmcMode", HTTP_ANY, [this]() {
        String htmlResponse = createRestartResponse("EMC Test Mode", "Rebooting into EMC test mode...");
        server->send(200, "text/html", htmlResponse);
        preferences.putBool("emc_mode", true);
        delay(1000);
        ESP.restart();
    });

    server->on("/resetEEPROM", HTTP_POST, [this]() {
        String htmlResponse = createRestartResponse("Restarting", "Restarting...");
        server->send(200, "text/html", htmlResponse);
        preferences.clear();
        preferences.end();
        delay(1000);
        ESP.restart();
    });

    server->on("/setDebugLevel", HTTP_POST, [this]() {
        if (server->hasArg("debugLevel")) {
            int debugLevel = server->arg("debugLevel").toInt();
            _logger.setDebugLevel(debugLevel);
            _logger.info("Debug level changed via web interface to: " + String(debugLevel));
        }
        server->send(204);
    });

    server->on("/setSerialOutputDisabled", HTTP_GET, [this]() {
        if (server->hasArg("disabled")) {
            String disabledStr = server->arg("disabled");
            bool disabled = (disabledStr == "true");
            _logger.setSerialOutputDisabled(disabled);
            _logger.info("Serial output " + String(disabled ? "disabled" : "enabled") + " via web interface");
            server->send(200, "text/plain", "Serial output " + String(disabled ? "disabled" : "enabled"));
        } else {
            server->send(400, "text/plain", "Missing disabled parameter");
        }
    });

    server->on("/version", HTTP_GET, [this]() {
        server->send(200, "text/plain", firmwareVersion);
    });
}

void WebServerManager::setupMotorControlRoutes() {
    server->on("/update_variable", HTTP_POST, [this]() {
        if (server->hasArg("new_setpoint_el")) {
            String update_var = server->arg("new_setpoint_el");
            if (update_var.length() != 0) {
                float el;
                bool flipMode = msc.isFlipModeEnabled();
                float minEl = flipMode ? 0.0f : (msc.isExtendedElEnabled() ? -90.0f : 0.0f);
                float maxEl = flipMode ? 180.0f : 90.0f;
                if (parseFloat(update_var, el) && el >= minEl && el <= maxEl) {
                    msc.setSetPointEl(flipMode ? MotorSensorController::flipToInternal(el) : el);
                }
            }
        }

        if (server->hasArg("new_setpoint_az")) {
            String update_var = server->arg("new_setpoint_az");
            float az;
            if (parseFloat(update_var, az)) {
                az = fmod(az, 360.0);
                if (az < 0) {
                    az += 360.0;
                }
                msc.setSetPointAz(az);
            }
        }
        server->send(204);
    });


    // In the motor control routes section, update the home function:
    server->on("/submitHome", HTTP_POST, [this]() {
        float homeAz = 0.0;
        float homeEl = msc.getHomeElInternal();

        // Check if wind-based home is enabled
        if (weatherPoller.isWindBasedHomeEnabled()) {
            homeAz = weatherPoller.getWindBasedHomePosition();
            _logger.info("Using wind-based home position: " + String(homeAz, 1) + "°");
        }

        msc.forceStopMs(100);
        msc.setSetPointAz(homeAz);
        msc.setSetPointEl(homeEl);
        server->send(204);
    });

    server->on("/stopMotors", HTTP_POST, [this]() {
        msc.forceStopMs(100);

        float az = fmod(msc.getCorrectedAngleAz(), 360.0f);
        if (az < 0) az += 360.0f;
        msc.setSetPointAz(az);

        float el = msc.getCorrectedAngleEl();
        if (el > 180.0f) el -= 360.0f;
        float minEl = msc.isExtendedElEnabled() ? -90.0f : 0.0f;
        el = constrain(el, minEl, 90.0f);
        msc.setSetPointEl(el);

        server->send(204);
    });

    server->on("/calon", HTTP_GET, [this]() {
        msc.activateCalMode(true);
        server->send(200, "text/plain", "Cal is On");
    });

    server->on("/caloff", HTTP_GET, [this]() {
        msc.activateCalMode(false);
        server->send(200, "text/plain", "Cal is Off");
    });

    server->on("/calEl", HTTP_GET, [this]() {
        msc.calibrate_elevation();
        server->send(200, "text/plain", "Cal Complete");
    });

    server->on("/moveAz", HTTP_GET, [this]() {
        if (server->hasArg("value")) {
            if (msc.calMode) {
                String value = server->arg("value");
                msc.calMoveMotor(value.c_str(), "AZ");
                server->send(200, "text/plain", "Azimuth moved to: " + value);
            } else {
                server->send(200, "text/plain", "Cal Mode OFF");
            }
        } else {
            server->send(400, "text/plain", "Value parameter missing");
        }
    });

    server->on("/moveEl", HTTP_GET, [this]() {
        if (server->hasArg("value")) {
            if (msc.calMode) {
                String value = server->arg("value");
                msc.calMoveMotor(value.c_str(), "EL");
                server->send(200, "text/plain", "Elevation moved to: " + value);
            } else {
                server->send(200, "text/plain", "Cal Mode OFF");
            }
        } else {
            server->send(400, "text/plain", "Value parameter missing");
        }
    });

    server->on("/setSingleMotorModeOn", HTTP_GET, [this]() {
        msc.singleMotorMode = true;
        preferences.putBool("singleMotorMode", msc.singleMotorMode);
        _logger.debug("SingleMotorMode On");
        server->send(200, "text/plain", "SingleMotorMode ON");
    });

    server->on("/setSingleMotorModeOff", HTTP_GET, [this]() {
        msc.singleMotorMode = false;
        preferences.putBool("singleMotorMode", msc.singleMotorMode);
        _logger.debug("SingleMotorMode OFF");
        server->send(200, "text/plain", "SingleMotorMode OFF");
    });

    server->on("/directionLockOn", HTTP_GET, [this]() {
        msc.setDirectionLockEnabled(true);
        server->send(200, "text/plain", "Direction lock ON");
    });

    server->on("/directionLockOff", HTTP_GET, [this]() {
        msc.setDirectionLockEnabled(false);
        server->send(200, "text/plain", "Direction lock OFF");
    });

    server->on("/extendedElOn", HTTP_GET, [this]() {
        msc.setExtendedElEnabled(true);
        server->send(200, "text/plain", "Extended elevation ON");
    });
    server->on("/extendedElOff", HTTP_GET, [this]() {
        msc.setExtendedElEnabled(false);
        server->send(200, "text/plain", "Extended elevation OFF");
    });

    server->on("/flipModeOn", HTTP_GET, [this]() {
        msc.setFlipModeEnabled(true);
        server->send(200, "text/plain", msc.isFlipModeEnabled() ? "Flip mode ON" : "Flip mode rejected (Negative Elevation off)");
    });
    server->on("/flipModeOff", HTTP_GET, [this]() {
        msc.setFlipModeEnabled(false);
        server->send(200, "text/plain", "Flip mode OFF");
    });

    server->on("/autoHomeOn", HTTP_GET, [this]() {
        msc.setAutoHomeEnabled(true);
        server->send(200, "text/plain", "Auto-home ON");
    });
    server->on("/autoHomeOff", HTTP_GET, [this]() {
        msc.setAutoHomeEnabled(false);
        server->send(200, "text/plain", "Auto-home OFF");
    });

    server->on("/smoothTrackingOn", HTTP_GET, [this]() {
        msc.setSmoothTrackingEnabled(true);
        server->send(200, "text/plain", "Smooth tracking ON");
    });
    server->on("/smoothTrackingOff", HTTP_GET, [this]() {
        msc.setSmoothTrackingEnabled(false);
        server->send(200, "text/plain", "Smooth tracking OFF");
    });

    server->on("/safeModeOn", HTTP_GET, [this]() {
        msc.setSafeMode(true);
        server->send(200, "text/plain", "Safe mode ON");
    });
    server->on("/safeModeOff", HTTP_GET, [this]() {
        msc.setSafeMode(false);
        server->send(200, "text/plain", "Safe mode OFF");
    });

}

void WebServerManager::setupConfigurationRoutes() {
    server->on("/setPassword", HTTP_POST, [this]() {
        if (server->hasArg("loginUser")) {
            String loginUser = server->arg("loginUser");
            setLoginUser(loginUser.c_str());
            preferences.putString("loginUser", loginUser);
        }

        if (server->hasArg("loginPassword")) {
            String loginPassword = server->arg("loginPassword");
            setLoginPassword(loginPassword.c_str());
            preferences.putString("loginPassword", loginPassword);
        }

        server->send(204);
    });

    server->on("/setWiFi", HTTP_POST, [this]() {
        bool hotspotMode = server->hasArg("hotspot");

        // WiFi channel lock for CE testing (0 = auto, 1-13 = locked)
        bool channelChanged = false;
        if (server->hasArg("wifi_chan_lock")) {
            int channel = server->arg("wifi_chan_lock").toInt();
            if (channel >= 0 && channel <= 13 && channel != preferences.getInt("wifi_chan_lock", 0)) {
                preferences.putInt("wifi_chan_lock", channel);
                channelChanged = true;
            }
        }

        if (hotspotMode) {
            wifi_ssid[0] = '\0';
            wifi_password[0] = '\0';
        } else if (server->hasArg("ssid") && server->hasArg("password")) {
            safeCopy(wifi_ssid, server->arg("ssid").c_str(), sizeof(wifi_ssid));
            safeCopy(wifi_password, server->arg("password").c_str(), sizeof(wifi_password));
        }

        if ((wifi_ssid[0] != '\0' && wifi_password[0] != '\0') || hotspotMode) {
            preferences.putString("wifi_ssid", wifi_ssid);
            preferences.putString("wifi_password", wifi_password);
            String htmlResponse = createRestartResponse("WiFi Credentials Updated!", "WiFi Credentials Updated! Restarting...");
            server->send(200, "text/html", htmlResponse);
            delay(1000);
            ESP.restart();
        } else if (channelChanged) {
            String htmlResponse = createRestartResponse("WiFi Channel Updated!", "WiFi Channel Updated! Restarting...");
            server->send(200, "text/html", htmlResponse);
            delay(1000);
            ESP.restart();
        } else {
            server->send(204);
        }
    });

    server->on("/setHostname", HTTP_POST, [this]() {
        if (server->hasArg("hostname")) {
            String hostname = server->arg("hostname");
            hostname.trim();
            if (hostname.length() > 0 && hostname.length() <= 32) {
                preferences.putString("hostname", hostname);
                String newUrl = "http://" + hostname + ".local:" + String(preferences.getInt("http_port", 80));
                String htmlResponse = "<!DOCTYPE html>\n"
                    "<html>\n<head>\n<title>Hostname Updated!</title>\n"
                    "<script>\n"
                    "  function enableButton() {\n"
                    "    var countdown = 10;\n"
                    "    var button = document.getElementById('backButton');\n"
                    "    var timer = setInterval(function() {\n"
                    "      button.innerHTML = 'Go Back (' + countdown + ')';\n"
                    "      countdown--;\n"
                    "      if (countdown < 0) {\n"
                    "        clearInterval(timer);\n"
                    "        button.innerHTML = 'Go Back';\n"
                    "        button.disabled = false;\n"
                    "      }\n"
                    "    }, 1000);\n"
                    "  }\n"
                    "</script>\n</head>\n"
                    "<body onload=\"enableButton()\">\n"
                    "<h1>Hostname Updated! Restarting...</h1>\n"
                    "<button id='backButton' onclick=\"window.location.href='" + newUrl + "'\" disabled>Go Back (10)</button>\n"
                    "</body>\n</html>\n";
                server->send(200, "text/html", htmlResponse);
                delay(1000);
                ESP.restart();
            } else {
                server->send(400, "text/plain", "Hostname must be 1-32 characters");
            }
        } else {
            server->send(400, "text/plain", "Missing hostname parameter");
        }
    });

    server->on("/setPorts", HTTP_POST, [this]() {
        bool updated = false;

        if (server->hasArg("http_port")) {
            String httpPortValue = server->arg("http_port");
            if (httpPortValue.length() > 0) {
                int http_port = httpPortValue.toInt();
                preferences.putInt("http_port", http_port);
                updated = true;
            }
        }

        if (server->hasArg("rotctl_port")) {
            String rotctlPortValue = server->arg("rotctl_port");
            if (rotctlPortValue.length() > 0) {
                int rotctl_port = rotctlPortValue.toInt();
                preferences.putInt("rotctl_port", rotctl_port);
                updated = true;
            }
        }

        if (updated) {
            String htmlResponse = createRestartResponse("Port Parameters Updated!", "Ports Updated! Restarting...");
            server->send(200, "text/html", htmlResponse);
            delay(1000);
            ESP.restart();
        } else {
            server->send(204);
        }
    });

    server->on("/setDualMotorMaxSpeed", HTTP_POST, [this]() {
        if (server->hasArg("maxDualMotorAzSpeed")) {
            String azSpeedValue = server->arg("maxDualMotorAzSpeed");
            if (azSpeedValue.length() > 0) {
                msc.max_dual_motor_az_speed = msc.convertPercentageToSpeed(azSpeedValue.toFloat(), msc.MIN_AZ_SPEED);
                preferences.putInt("maxDMAzSpeed", msc.max_dual_motor_az_speed);
            }
        }

        if (server->hasArg("maxDualMotorElSpeed")) {
            String elSpeedValue = server->arg("maxDualMotorElSpeed");
            if (elSpeedValue.length() > 0) {
                msc.max_dual_motor_el_speed = msc.convertPercentageToSpeed(elSpeedValue.toFloat(), msc.MIN_EL_SPEED);
                preferences.putInt("maxDMElSpeed", msc.max_dual_motor_el_speed);
            }
        }
        server->send(204);
    });

    server->on("/setSingleMotorMaxSpeed", HTTP_POST, [this]() {
        if (server->hasArg("maxSingleMotorAzSpeed")) {
            String azSpeedValue = server->arg("maxSingleMotorAzSpeed");
            if (azSpeedValue.length() > 0) {
                msc.max_single_motor_az_speed = msc.convertPercentageToSpeed(azSpeedValue.toFloat(), msc.MIN_AZ_SPEED);
                preferences.putInt("maxSMAzSpeed", msc.max_single_motor_az_speed);
            }
        }

        if (server->hasArg("maxSingleMotorElSpeed")) {
            String elSpeedValue = server->arg("maxSingleMotorElSpeed");
            if (elSpeedValue.length() > 0) {
                msc.max_single_motor_el_speed = msc.convertPercentageToSpeed(elSpeedValue.toFloat(), msc.MIN_EL_SPEED);
                preferences.putInt("maxSMElSpeed", msc.max_single_motor_el_speed);
            }
        }
        server->send(204);
    });

    server->on("/setStellarium", HTTP_POST, [this]() {
        if (server->hasArg("stellariumServerIP")) {
            String serverIP = server->arg("stellariumServerIP");
            if (serverIP.length() > 0) {
                preferences.putString("stelServIP", serverIP);
            }
        }

        if (server->hasArg("stellariumServerPort")) {
            String serverPort = server->arg("stellariumServerPort");
            if (serverPort.length() > 0) {
                preferences.putString("stelServPort", serverPort);
            }
        }
        server->send(204);
    });

    server->on("/setAdvancedParams", HTTP_POST, [this]() {
        bool updated = false;
        
        // Helper lambda for parameter validation and setting
        auto setIntParam = [&](const String& argName, auto setter, int minVal, int maxVal) {
            if (server->hasArg(argName)) {
                String argValue = server->arg(argName);
                argValue.trim();
                if (argValue.length() > 0) {
                    int value = argValue.toInt();
                    if (value >= minVal && value <= maxVal) {
                        setter(value);
                        updated = true;
                    }
                }
            }
        };

        auto setFloatParam = [&](const String& argName, auto setter, float minVal, float maxVal) {
            if (server->hasArg(argName)) {
                String argValue = server->arg(argName);
                argValue.trim();
                if (argValue.length() > 0) {
                    float value = argValue.toFloat();
                    if (value >= minVal && value <= maxVal) {
                        setter(value);
                        updated = true;
                    }
                }
            }
        };

        setIntParam("P_el", [this](int v) { msc.setPEl(v); }, 0, 1000);
        setIntParam("P_az", [this](int v) { msc.setPAz(v); }, 0, 1000);
        setIntParam("MIN_EL_SPEED", [this](int v) { msc.setMinElSpeed(v); }, 0, 255);
        setIntParam("MIN_AZ_SPEED", [this](int v) { msc.setMinAzSpeed(v); }, 0, 255);
        setIntParam("MAX_FAULT_POWER_AZ", [this](int v) { msc.setMaxPowerFaultAz(v); }, 1, 25);
        setIntParam("MAX_FAULT_POWER_EL", [this](int v) { msc.setMaxPowerFaultEl(v); }, 1, 25);
        setIntParam("MAX_FAULT_POWER_TOTAL", [this](int v) { msc.setMaxPowerFaultTotal(v); }, 1, 25);
        setIntParam("MIN_VOLTAGE_THRESHOLD", [this](int v) { msc.setMinVoltageThreshold(v); }, 1, 20);
        
        setFloatParam("MIN_AZ_TOLERANCE", [this](float v) { msc.setMinAzTolerance(v); }, 0.01f, 10.0f);
        setFloatParam("MIN_EL_TOLERANCE", [this](float v) { msc.setMinElTolerance(v); }, 0.01f, 10.0f);
        setFloatParam("I_el", [this](float v) { msc.setIEl(v); }, 0.0f, 1000.0f);
        setFloatParam("I_az", [this](float v) { msc.setIAz(v); }, 0.0f, 1000.0f);
        setFloatParam("D_el", [this](float v) { msc.setDEl(v); }, 0.0f, 1000.0f);
        setFloatParam("D_az", [this](float v) { msc.setDAz(v); }, 0.0f, 1000.0f);
        setIntParam("autoHomeMins", [this](int v) { msc.setAutoHomeTimeout(v); }, 1, 60);

        setFloatParam("smKalQ", [this](float v) { msc.setKalmanQ(v); }, 0.01f, 100.0f);
        setFloatParam("smKalR", [this](float v) { msc.setKalmanR(v); }, 0.01f, 100.0f);
        setIntParam("smMinAzSpd", [this](int v) { msc.setMinSmoothAzSpeed(v); }, 0, 255);
        setIntParam("smMinElSpd", [this](int v) { msc.setMinSmoothElSpeed(v); }, 0, 255);

        if (updated) {
            _logger.info("Advanced parameters updated via web interface");
        }
        
        server->send(204);
    });

    // Weather configuration routes

    server->on("/setWeatherApiKey", HTTP_POST, [this]() {
        if (server->hasArg("weatherApiKey")) {
            String apiKey = server->arg("weatherApiKey");
            apiKey.trim();
            
            if (apiKey.length() == 0) {
                // Allow clearing the API key
                weatherPoller.setApiKey("");
                _logger.info("Weather API key cleared via web interface");
                server->send(200, "text/plain", "API key cleared");
            } else if (weatherPoller.setApiKey(apiKey)) {
                _logger.info("Weather API key updated via web interface");
                server->send(204);
            } else {
                server->send(400, "text/plain", "Invalid API key format");
            }
        } else {
            server->send(400, "text/plain", "Missing API key parameter");
        }
    });


    server->on("/setWeatherLocation", HTTP_POST, [this]() {
        bool updated = false;
        
        if (server->hasArg("latitude") && server->hasArg("longitude")) {
            String latStr = server->arg("latitude");
            String lonStr = server->arg("longitude");
            
            latStr.trim();
            lonStr.trim();
            
            if (latStr.length() > 0 && lonStr.length() > 0) {
                float lat = latStr.toFloat();
                float lon = lonStr.toFloat();
                
                _logger.debug("Received weather location: " + String(lat, 6) + ", " + String(lon, 6));
                
                if (weatherPoller.setLocation(lat, lon)) {
                    updated = true;
                    _logger.info("Weather location updated via web interface: " + 
                                String(lat, 6) + ", " + String(lon, 6));
                } else {
                    _logger.error("Invalid coordinates received: " + String(lat, 6) + ", " + String(lon, 6));
                    server->send(400, "text/plain", "Invalid coordinates");
                    return;
                }
            }
        } else {
            _logger.error("Missing latitude or longitude parameters");
        }
        
        if (updated) {
            server->send(204);
        } else {
            server->send(400, "text/plain", "Missing or invalid coordinates");
        }
    });

    server->on("/weatherOn", HTTP_GET, [this]() {
        weatherPoller.setPollingEnabled(true);
        server->send(200, "text/plain", "Weather polling ON");
    });

    server->on("/weatherOff", HTTP_GET, [this]() {
        weatherPoller.setPollingEnabled(false);
        server->send(200, "text/plain", "Weather polling OFF");
    });

    server->on("/forceWeatherUpdate", HTTP_GET, [this]() {
        if (weatherPoller.isLocationConfigured()) {
            weatherPoller.forceUpdate();
            server->send(200, "text/plain", "Weather update forced");
        } else {
            server->send(400, "text/plain", "Location not configured");
        }
    });


    // Wind safety configuration routes
    server->on("/windSafetyOn", HTTP_GET, [this]() {
        weatherPoller.setWindSafetyEnabled(true);
        server->send(200, "text/plain", "Wind safety ON");
    });

    server->on("/windSafetyOff", HTTP_GET, [this]() {
        weatherPoller.setWindSafetyEnabled(false);
        server->send(200, "text/plain", "Wind safety OFF");
    });

    server->on("/windBasedHomeOn", HTTP_GET, [this]() {
        weatherPoller.setWindBasedHomeEnabled(true);
        server->send(200, "text/plain", "Wind-based home positioning ON");
    });

    server->on("/windBasedHomeOff", HTTP_GET, [this]() {
        weatherPoller.setWindBasedHomeEnabled(false);
        server->send(200, "text/plain", "Wind-based home positioning OFF");
    });

    server->on("/showSunMoonOn", HTTP_GET, [this]() {
        weatherPoller.setShowSunMoon(true);
        server->send(200, "text/plain", "Sun/Moon compass display ON");
    });

    server->on("/showSunMoonOff", HTTP_GET, [this]() {
        weatherPoller.setShowSunMoon(false);
        server->send(200, "text/plain", "Sun/Moon compass display OFF");
    });

    server->on("/unitsImperial", HTTP_GET, [this]() {
        weatherPoller.setUseImperial(true);
        server->send(200, "text/plain", "Weather units set to Imperial");
    });

    server->on("/unitsMetric", HTTP_GET, [this]() {
        weatherPoller.setUseImperial(false);
        server->send(200, "text/plain", "Weather units set to Metric");
    });

    server->on("/setWindThresholds", HTTP_POST, [this]() {
        bool updated = false;
        
        if (server->hasArg("windSpeedThreshold")) {
            String speedStr = server->arg("windSpeedThreshold");
            speedStr.trim();
            if (speedStr.length() > 0) {
                float speed = speedStr.toFloat();
                if (speed >= 10.0 && speed <= 200.0) {
                    weatherPoller.setWindSpeedThreshold(speed);
                    updated = true;
                    _logger.info("Wind speed threshold set to: " + String(speed, 1) + " km/h");
                }
            }
        }
        
        if (server->hasArg("windGustThreshold")) {
            String gustStr = server->arg("windGustThreshold");
            gustStr.trim();
            if (gustStr.length() > 0) {
                float gust = gustStr.toFloat();
                if (gust >= 10.0 && gust <= 200.0) {
                    weatherPoller.setWindGustThreshold(gust);
                    updated = true;
                    _logger.info("Wind gust threshold set to: " + String(gust, 1) + " km/h");
                }
            }
        }
        
        if (updated) {
            server->send(204);
        } else {
            server->send(400, "text/plain", "Invalid threshold values");
        }
    });

    server->on("/setAngleOffsets", HTTP_POST, [this]() {
        bool updated = false;
        
        if (server->hasArg("azOffset")) {
            String azOffsetStr = server->arg("azOffset");
            azOffsetStr.trim();
            if (azOffsetStr.length() > 0) {
                float azOffset = azOffsetStr.toFloat();
                if (azOffset >= -180.0 && azOffset <= 180.0) {
                    msc.setAzOffset(azOffset);
                    updated = true;
                    _logger.info("AZ angle offset set to: " + String(azOffset, 3) + "° via web interface");
                } else {
                    _logger.warn("AZ offset out of range: " + String(azOffset, 3) + "°");
                }
            }
        }
        
        if (server->hasArg("elOffset")) {
            String elOffsetStr = server->arg("elOffset");
            elOffsetStr.trim();
            if (elOffsetStr.length() > 0) {
                float elOffset = elOffsetStr.toFloat();
                if (elOffset >= -5.0 && elOffset <= 5.0) {
                    msc.setElOffset(elOffset);
                    updated = true;
                    _logger.info("EL angle offset set to: " + String(elOffset, 3) + "° via web interface");
                } else {
                    _logger.warn("EL offset out of range: " + String(elOffset, 3) + "°");
                }
            }
        }
        
        if (updated) {
            server->send(204); // Success, no content
        } else {
            server->send(400, "text/plain", "Invalid offset values or out of range");
        }
    });



}

void WebServerManager::setupAPIRoutes() {
    server->on("/stellariumOn", HTTP_GET, [this]() {
        stellariumPoller.setStellariumOn(true);
        preferences.putBool("stellariumOn", true);
        server->send(200, "text/plain", "Stellarium ON");
    });

    server->on("/stellariumOff", HTTP_GET, [this]() {
        stellariumPoller.setStellariumOn(false);
        preferences.putBool("stellariumOn", false);
        server->send(200, "text/plain", "Stellarium OFF");
    });

    // Real-time status endpoint - polled frequently, no NVS reads
    server->on("/status", HTTP_GET, [this]() {
        static DynamicJsonDocument doc(4096);
        doc.clear();

        // Motor and control data — use native numeric types to avoid String heap allocations
        float displayEl = msc.getCorrectedAngleEl();
        if (displayEl > 180.0f) displayEl -= 360.0f;
        float setpointEl = msc.getSetPointEl();
        // In flip mode the user-facing axis is [0, 180] = 90° - internal. The frontend
        // also receives the raw internal values so the polar plot can stay correct.
        bool flipMode = msc.isFlipModeEnabled();
        doc["correctedAngle_el"] = r2(flipMode ? MotorSensorController::internalToFlip(displayEl) : displayEl);
        doc["correctedAngle_el_internal"] = r2(displayEl);
        doc["correctedAngle_az"] = r2(msc.getCorrectedAngleAz());
        doc["setpoint_az"] = r2(msc.getSetPointAz());
        doc["setpoint_el"] = r2(flipMode ? MotorSensorController::internalToFlip(setpointEl) : setpointEl);
        doc["setpoint_el_internal"] = r2(setpointEl);
        doc["setPointState_az"] = (int)msc.setPointState_az.load();
        doc["setPointState_el"] = (int)msc.setPointState_el.load();
        doc["error_az"] = r2((float)msc.getErrorAz());
        // Flip mode reverses the EL axis (flip = 90 - internal), so error sign
        // flips too — keep the displayed error consistent with the displayed
        // setpoint and corrected angle.
        doc["error_el"] = r2((float)msc.getErrorEl() * (flipMode ? -1.0f : 1.0f));
        doc["el_startAngle"] = r2(msc.getElStartAngle());
        doc["needs_unwind"] = msc.needs_unwind.load();

        // Status flags
        doc["i2cErrorFlag_az"] = (int)msc.i2cErrorFlag_az.load();
        doc["i2cErrorFlag_el"] = (int)msc.i2cErrorFlag_el.load();
        doc["faultTripped"] = (int)msc.global_fault.load();
        doc["badAngleFlag"] = (int)msc.badAngleFlag.load();
        doc["magnetFault"] = (int)msc.magnetFault.load();
        doc["isAzMotorLatched"] = (int)msc._isAzMotorLatched.load();
        doc["isElMotorLatched"] = (int)msc._isElMotorLatched.load();
        doc["motorSpeedPctAz"] = msc.getMotorSpeedPctAz();
        doc["motorSpeedPctEl"] = msc.getMotorSpeedPctEl();
        // Power data
        doc["inputVoltage"] = r2(ina219Manager.getLoadVoltage());
        doc["currentDraw"] = r2(ina219Manager.getCurrent() / 1000);
        doc["rotatorPowerDraw"] = r2(ina219Manager.getPower());

        // Log messages — circular buffer is already bounded, no truncation needed
        {
            String logMsgs = _logger.getNewLogMessages();
            doc["newLogMessages"] = logMsgs;
        }
        doc["currentDebugLevel"] = _logger.getDebugLevel();

        if (msc.isSmoothTrackingEnabled()) {
            float kalmanEl = msc.getKalmanElPos();
            doc["kalmanAzPos"] = r2(msc.getKalmanAzPos());
            // -1.0f is the literal sentinel for "Kalman not initialized" — pass through
            // untranslated so the frontend's existing sentinel handling still works.
            doc["kalmanElPos"] = r2((flipMode && kalmanEl != -1.0f) ? MotorSensorController::internalToFlip(kalmanEl) : kalmanEl);
            doc["kalmanElPos_internal"] = r2(kalmanEl);
            doc["kalmanAzVel"] = r2(msc.getKalmanAzVel());
            doc["kalmanElVel"] = r2(msc.getKalmanElVel());
        }

        static String json;
        json = "";                             // keep buffer, reset length
        json.reserve(measureJson(doc) + 1);    // no-op once buffer is large enough
        serializeJson(doc, json);
        server->send(200, "application/json", json);
    });

    // Slow-changing status endpoint - polled at 2s interval
    server->on("/info", HTTP_GET, [this]() {
        static DynamicJsonDocument doc(6144);
        doc.clear();

        // Network data
        int rssi = wifiManager.getRSSI();
        doc["rssi"] = rssi;
        doc["level"] = wifiManager.getSignalStrengthLevel(rssi);
        doc["ip_addr"] = wifiManager.ip_addr;
        doc["rotctl_client_ip"] = rotctlWifi.getRotctlClientIP();
        doc["bssid"] = wifiManager.getCurrentBSSID();
        doc["wifi_channel"] = wifiManager.getCurrentWiFiChannel();

        // Slow-changing status flags
        doc["extendedElEnabled"] = msc.isExtendedElEnabled() ? "ON" : "OFF";
        doc["flipModeEnabled"] = msc.isFlipModeEnabled() ? "ON" : "OFF";
        doc["stellariumConnActive"] = stellariumPoller.getStellariumConnActive() ? "Connected" : "Disconnected";
        doc["serialOutputDisabled"] = _logger.getSerialOutputDisabled();
        doc["calMode"] = msc.calMode ? "ON" : "OFF";
        doc["serialActive"] = (int)serialManager.serialActive.load();
        doc["singleMotorModeText"] = msc.singleMotorMode ? "ON" : "OFF";
        doc["directionLockEnabled"] = msc.isDirectionLockEnabled() ? "ON" : "OFF";
        doc["safeMode"] = msc.isSafeMode() ? "ON" : "OFF";
        doc["smoothTrackingActive"] = msc.isSmoothTrackingEnabled() ? "ON" : "OFF";

        // Weather data (in-memory via mutex)
        WeatherData weatherData = weatherPoller.getWeatherData();
        if (weatherData.dataValid) {
            doc["weatherDataValid"] = "YES";
            doc["currentWindSpeed"] = r1(weatherData.currentWindSpeed);
            doc["currentWindDirection"] = r1(weatherData.currentWindDirection);
            doc["currentWindGust"] = r1(weatherData.currentWindGust);
            doc["currentWeatherTime"] = weatherData.currentTime;
            doc["currentTempC"] = r1(weatherData.currentTempC);
            doc["currentPrecipMm"] = r1(weatherData.currentPrecipMm);
            doc["currentHumidity"] = weatherData.currentHumidity;
            doc["currentConditionText"] = weatherData.currentConditionText;
            doc["currentIsThunderstorm"] = weatherData.currentIsThunderstorm ? "YES" : "NO";

            JsonArray forecastWindSpeed = doc.createNestedArray("forecastWindSpeed");
            JsonArray forecastWindDirection = doc.createNestedArray("forecastWindDirection");
            JsonArray forecastWindGust = doc.createNestedArray("forecastWindGust");
            JsonArray forecastTimes = doc.createNestedArray("forecastTimes");
            JsonArray forecastTempC = doc.createNestedArray("forecastTempC");
            JsonArray forecastPrecipMm = doc.createNestedArray("forecastPrecipMm");
            JsonArray forecastSnowCm = doc.createNestedArray("forecastSnowCm");
            JsonArray forecastHumidity = doc.createNestedArray("forecastHumidity");
            JsonArray forecastConditionText = doc.createNestedArray("forecastConditionText");
            JsonArray forecastIsThunderstorm = doc.createNestedArray("forecastIsThunderstorm");
            for (int i = 0; i < 3; i++) {
                forecastWindSpeed.add(r1(weatherData.forecastWindSpeed[i]));
                forecastWindDirection.add(r1(weatherData.forecastWindDirection[i]));
                forecastWindGust.add(r1(weatherData.forecastWindGust[i]));
                forecastTimes.add(weatherData.forecastTimes[i]);
                forecastTempC.add(r1(weatherData.forecastTempC[i]));
                forecastPrecipMm.add(r1(weatherData.forecastPrecipMm[i]));
                forecastSnowCm.add(r1(weatherData.forecastSnowCm[i]));
                forecastHumidity.add(weatherData.forecastHumidity[i]);
                forecastConditionText.add(weatherData.forecastConditionText[i]);
                forecastIsThunderstorm.add(weatherData.forecastIsThunderstorm[i] ? "YES" : "NO");
            }

            doc["weatherLastUpdate"] = weatherData.lastUpdateTime;
            doc["weatherError"] = "";
        } else {
            doc["weatherDataValid"] = "NO";
            doc["currentWindSpeed"] = "N/A";
            doc["currentWindDirection"] = "N/A";
            doc["currentWindGust"] = "N/A";
            doc["currentWeatherTime"] = "N/A";
            doc["weatherLastUpdate"] = "Never";
            doc["weatherError"] = weatherPoller.getLastError();
        }

        // Wind safety data (in-memory via mutex)
        auto windSafetyData = weatherPoller.getWindSafetyData();
        WindStowState windStowState = msc.getWindStowState();
        doc["windStowActive"] = windStowState.active ? "YES" : "NO";
        doc["windStowReason"] = windStowState.reason;
        doc["stowDirection"] = r1(windSafetyData.currentStowDirection);
        doc["windTrackingActive"] = msc.isWindTrackingActive() ? "YES" : "NO";
        doc["emergencyStowActive"] = windSafetyData.emergencyStowActive ? "YES" : "NO";

        static String json;
        json = "";                             // keep buffer, reset length
        json.reserve(measureJson(doc) + 1);    // no-op once buffer is large enough
        serializeJson(doc, json);
        server->send(200, "application/json", json);
    });

    // Configuration endpoint - fetched once on page load and after settings changes
    server->on("/config", HTTP_GET, [this]() {
        static DynamicJsonDocument doc(6144);
        doc.clear();

        // Network configuration (NVS reads)
        doc["http_port"] = preferences.getInt("http_port", 80);
        doc["rotctl_port"] = preferences.getInt("rotctl_port", 4533);
        doc["hostname"] = preferences.getString("hostname", "discoverydrive");
        doc["wifissid"] = preferences.getString("wifi_ssid", "discoverydish_HOTSPOT");
        doc["wifiChanLock"] = preferences.getInt("wifi_chan_lock", 0);

        // Authentication (NVS reads)
        doc["loginUser"] = preferences.getString("loginUser", "");
        bool hasAuth = (preferences.getString("loginUser", "").length() != 0 &&
                        preferences.getString("loginPassword", "").length() != 0 &&
                        _loginRequired);
        doc["passwordStatus"] = hasAuth ? "True" : "False";

        // Motor speed configuration
        doc["maxDualMotorAzSpeed"] = msc.convertSpeedToPercentage((float)msc.max_dual_motor_az_speed, msc.MIN_AZ_SPEED);
        doc["maxDualMotorElSpeed"] = msc.convertSpeedToPercentage((float)msc.max_dual_motor_el_speed, msc.MIN_EL_SPEED);
        doc["maxSingleMotorAzSpeed"] = msc.convertSpeedToPercentage((float)msc.max_single_motor_az_speed, msc.MIN_AZ_SPEED);
        doc["maxSingleMotorElSpeed"] = msc.convertSpeedToPercentage((float)msc.max_single_motor_el_speed, msc.MIN_EL_SPEED);

        // Stellarium configuration (NVS reads)
        doc["stellariumPollingOn"] = preferences.getBool("stellariumOn", false) ? "ON" : "OFF";
        doc["stellariumServerIPText"] = preferences.getString("stelServIP", "NO IP SET");
        doc["stellariumServerPortText"] = preferences.getString("stelServPort", "8090");

        // Motor tuning parameters (in-memory)
        doc["toleranceAz"] = r2(msc.getMinAzTolerance());
        doc["toleranceEl"] = r2(msc.getMinElTolerance());
        doc["P_el"] = msc.getPEl();
        doc["P_az"] = msc.getPAz();
        doc["MIN_EL_SPEED"] = msc.getMinElSpeed();
        doc["MIN_AZ_SPEED"] = msc.getMinAzSpeed();
        doc["MIN_AZ_TOLERANCE"] = r2(msc.getMinAzTolerance());
        doc["MIN_EL_TOLERANCE"] = r2(msc.getMinElTolerance());
        doc["MAX_FAULT_POWER_AZ"] = msc.getMaxPowerFaultAz();
        doc["MAX_FAULT_POWER_EL"] = msc.getMaxPowerFaultEl();
        doc["MAX_FAULT_POWER_TOTAL"] = msc.getMaxPowerFaultTotal();
        doc["MIN_VOLTAGE_THRESHOLD"] = msc.getMinVoltageThreshold();
        doc["I_el"] = r2(msc.getIEl());
        doc["I_az"] = r2(msc.getIAz());
        doc["D_el"] = r2(msc.getDEl());
        doc["D_az"] = r2(msc.getDAz());
        doc["azOffset"] = r3(msc.getAzOffset());
        doc["elOffset"] = r3(msc.getElOffset());
        doc["singleMotorModeText"] = msc.singleMotorMode ? "ON" : "OFF";
        doc["directionLockEnabled"] = msc.isDirectionLockEnabled() ? "ON" : "OFF";
        doc["safeMode"] = msc.isSafeMode() ? "ON" : "OFF";
        doc["smoothTrackingActive"] = msc.isSmoothTrackingEnabled() ? "ON" : "OFF";
        doc["autoHomeEnabled"] = msc.isAutoHomeEnabled() ? "ON" : "OFF";
        doc["autoHomeMins"] = msc.getAutoHomeTimeout();
        doc["smoothTrackingEnabled"] = msc.isSmoothTrackingEnabled() ? "ON" : "OFF";
        doc["smKalQ"] = r2(msc.getKalmanQ());
        doc["smKalR"] = r2(msc.getKalmanR());
        doc["smMinAzSpd"] = msc.getMinSmoothAzSpeed();
        doc["smMinElSpd"] = msc.getMinSmoothElSpeed();

        // Weather configuration
        doc["weatherEnabled"] = weatherPoller.isPollingEnabled() ? "ON" : "OFF";
        doc["weatherApiKeyConfigured"] = weatherPoller.isApiKeyConfigured() ? "YES" : "NO";
        doc["weatherLocationConfigured"] = weatherPoller.isLocationConfigured() ? "YES" : "NO";
        doc["weatherLatitude"] = weatherPoller.getLatitude();
        doc["weatherLongitude"] = weatherPoller.getLongitude();

        // Sun/Moon display
        doc["showSunMoon"] = weatherPoller.isShowSunMoon() ? "ON" : "OFF";

        // Weather units
        doc["weatherUnits"] = weatherPoller.isUseImperial() ? "Imperial" : "Metric";

        // Wind safety configuration
        doc["windSafetyEnabled"] = weatherPoller.isWindSafetyEnabled() ? "ON" : "OFF";
        doc["windBasedHomeEnabled"] = weatherPoller.isWindBasedHomeEnabled() ? "ON" : "OFF";
        doc["windSpeedThreshold"] = r1(weatherPoller.getWindSpeedThreshold());
        doc["windGustThreshold"] = r1(weatherPoller.getWindGustThreshold());

        static String json;
        json = "";                             // keep buffer, reset length
        json.reserve(measureJson(doc) + 1);    // no-op once buffer is large enough
        serializeJson(doc, json);
        server->send(200, "application/json", json);
    });
}

void WebServerManager::setupDebugRoutes() {
    // Catch-all handler for debugging
    server->onNotFound([this]() {
        String method = (server->method() == HTTP_GET) ? "GET" : 
                       (server->method() == HTTP_POST) ? "POST" : "OTHER";
        
        _logger.debug("404 - " + method + " " + server->uri());
        
        if (server->method() == HTTP_POST) {
            String contentType = server->header("Content-Type");
            _logger.debug("Content-Type: " + contentType);
        }
        
        server->send(404, "text/plain", "Not Found: " + method + " " + server->uri());
    });
}

// [Rest of the file remains the same - all the OTA upload methods, utility methods, etc.]
// =============================================================================
// UPLOAD ROUTE SETUP METHODS
// =============================================================================

void WebServerManager::setupFileUploadRoute() {
    server->on("/fileupdate", HTTP_POST, 
        [this]() {
            String html = "<!DOCTYPE html><html><head><title>Upload Complete</title>";
            html += "<style>body{font-family:Arial;margin:40px;text-align:center;}";
            html += ".success{background:#d4edda;color:#155724;padding:20px;border-radius:5px;margin:20px 0;}";
            html += ".error{background:#f8d7da;color:#721c24;padding:20px;border-radius:5px;margin:20px 0;}";
            html += "button{background:#4CAF50;color:white;padding:10px 20px;border:none;border-radius:5px;cursor:pointer;margin:10px;}";
            html += "button:hover{background:#45a049;}</style></head><body>";
            html += "<h1>Upload Complete</h1>";
            html += "<div class='success'>File uploaded successfully!</div>";
            html += "<button onclick=\"window.location.href='/ota'\">Upload Another</button>";
            html += "<button onclick=\"window.location.href='/'\">Home</button>";
            html += "</body></html>";
            
            server->send(200, "text/html", html);
        }, 
        [this]() {
            handleFileUpload();
        }
    );
}

void WebServerManager::setupFirmwareUploadRoute() {
    server->on("/firmware", HTTP_POST,
        [this]() {
            if (_firmwareUpdateSuccess) {
                String html = "<!DOCTYPE html><html><head><title>Firmware Update</title>";
                html += "<style>body{font-family:Arial;margin:40px;text-align:center;}";
                html += ".success{background:#d4edda;color:#155724;padding:20px;border-radius:5px;margin:20px 0;}";
                html += "</style></head><body>";
                html += "<h1>Firmware Update Complete</h1>";
                html += "<div class='success'>Firmware updated successfully! Device will restart...</div>";
                html += "<script>setTimeout(function(){ window.location.href='/'; }, 3000);</script>";
                html += "</body></html>";

                server->send(200, "text/html", html);

                delay(1000);
                ESP.restart();
            } else {
                String html = "<!DOCTYPE html><html><head><title>Firmware Update Failed</title>";
                html += "<style>body{font-family:Arial;margin:40px;text-align:center;}";
                html += ".error{background:#f8d7da;color:#721c24;padding:20px;border-radius:5px;margin:20px 0;}";
                html += "button{background:#4CAF50;color:white;padding:10px 20px;border:none;border-radius:5px;cursor:pointer;font-size:16px;margin:10px;}";
                html += "button:hover{background:#45a049;}</style></head><body>";
                html += "<h1>Firmware Update Failed</h1>";
                html += "<div class='error'>" + String(_firmwareUpdateError) + "</div>";
                html += "<button onclick=\"window.location.href='/ota'\">Try Again</button>";
                html += "<button onclick=\"window.location.href='/'\">Back to Main</button>";
                html += "</body></html>";

                server->send(400, "text/html", html);
            }
        },
        [this]() {
            handleFirmwareUpload();
        }
    );
}

// =============================================================================
// OTA UPDATE METHODS
// =============================================================================

void WebServerManager::handleOTAUpload() {
    const char* loginUser = getLoginUser();
    const char* loginPassword = getLoginPassword();

    if (_loginRequired && loginUser[0] != '\0' && loginPassword[0] != '\0') {
        if (!server->authenticate(loginUser, loginPassword)) {
            return server->requestAuthentication();
        }
    }

    String html = generateOTAUploadHTML();
    server->send(200, "text/html", html);
}

void WebServerManager::handleFileUpload() {
    HTTPUpload& upload = server->upload();
    
    // Group all state into a struct for cleaner management
    struct UploadState {
        String filename;
        File file;
        bool success;
        size_t bytesWritten;
        bool mutexHeld;
        unsigned long startTime;
        
        void reset() {
            if (file) {
                file.close();
            }
            filename = "";
            file = File();
            success = false;
            bytesWritten = 0;
            mutexHeld = false;
            startTime = 0;
        }
        
        bool isStale(unsigned long timeoutMs = 30000) {
            // Consider upload stale if no activity for 30 seconds
            return (startTime > 0 && (millis() - startTime) > timeoutMs);
        }
    };
    
    static UploadState state;
    static const size_t MAX_UPLOAD_SIZE = 3 * 1024 * 1024;
    static const unsigned long UPLOAD_TIMEOUT_MS = 30000;

    // Detect and clean up stale uploads before processing
    if (state.startTime > 0 && state.isStale(UPLOAD_TIMEOUT_MS)) {
        _logger.warn("Cleaning up stale upload: " + state.filename);
        if (state.file) {
            state.file.close();
        }
        if (state.mutexHeld && _fileMutex != NULL) {
            xSemaphoreGive(_fileMutex);
        }
        state.reset();
    }

    if (upload.status == UPLOAD_FILE_START) {
        _logger.info("Starting upload: " + String(upload.filename.c_str()));
        
        // Clean up any previous incomplete upload
        if (state.file) {
            _logger.warn("Cleaning up previous incomplete upload");
            state.file.close();
        }
        if (state.mutexHeld && _fileMutex != NULL) {
            xSemaphoreGive(_fileMutex);
        }
        
        // Reset all state for new upload
        state.reset();
        state.filename = upload.filename;
        state.startTime = millis();
        
        if (!isValidUpdateFile(upload.filename)) {
            _logger.error("Invalid file type: " + upload.filename);
            return;
        }
        
        String filepath = "/" + upload.filename;
        
        if (_fileMutex != NULL && xSemaphoreTake(_fileMutex, pdMS_TO_TICKS(5000)) == pdTRUE) {
            state.mutexHeld = true;
            state.file = LittleFS.open(filepath, "w");
            
            if (state.file) {
                _logger.debug("File opened for writing: " + filepath);
                state.success = true;
            } else {
                _logger.error("Cannot open file: " + filepath);
                xSemaphoreGive(_fileMutex);
                state.mutexHeld = false;
            }
        } else {
            _logger.error("Cannot acquire file mutex (timeout)");
        }
        
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        // Update activity timestamp
        state.startTime = millis();
        
        _logger.debug("Writing " + String(upload.currentSize) + " bytes (total: " + 
                     String(state.bytesWritten + upload.currentSize) + ")");

        // Check file size limit
        if (state.bytesWritten + upload.currentSize > MAX_UPLOAD_SIZE) {
            _logger.error("File too large - exceeds " + String(MAX_UPLOAD_SIZE) + " byte limit");
            state.success = false;
            
            // Clean up immediately
            if (state.file) {
                state.file.close();
                state.file = File();
                LittleFS.remove("/" + state.filename);  // Delete partial file
            }
            if (state.mutexHeld && _fileMutex != NULL) {
                xSemaphoreGive(_fileMutex);
                state.mutexHeld = false;
            }
            return;
        }
        
        if (state.file && state.success && upload.currentSize > 0) {
            size_t written = state.file.write(upload.buf, upload.currentSize);
            if (written != upload.currentSize) {
                _logger.error("Write failed - expected " + String(upload.currentSize) + 
                             ", wrote " + String(written));
                state.success = false;
            } else {
                state.bytesWritten += written;
            }
        } else if (!state.file) {
            _logger.error("File handle invalid during write");
            state.success = false;
        }
        
    } else if (upload.status == UPLOAD_FILE_END) {
        _logger.debug("Upload finished: " + state.filename + ", total: " + 
                     String(upload.totalSize) + " bytes (written: " + 
                     String(state.bytesWritten) + ")");

        // Close file first
        if (state.file) {
            state.file.close();
            state.file = File();
        }
        
        // Release mutex
        if (state.mutexHeld && _fileMutex != NULL) {
            xSemaphoreGive(_fileMutex);
            state.mutexHeld = false;
        }
        
        // Verify and log result
        if (state.success && state.bytesWritten > 0) {
            _logger.info("File uploaded: " + state.filename + " (" + 
                        String(state.bytesWritten) + " bytes)");
            verifyUploadedFile(state.filename, state.bytesWritten);
        } else {
            _logger.error("Upload failed: " + state.filename);
            // Clean up failed upload file
            LittleFS.remove("/" + state.filename);
        }
        
        // Reset state for next upload
        state.reset();
        
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        _logger.warn("Upload aborted: " + state.filename);
        
        // Close and delete partial file
        if (state.file) {
            state.file.close();
            state.file = File();
            LittleFS.remove("/" + state.filename);
        }
        
        // Release mutex
        if (state.mutexHeld && _fileMutex != NULL) {
            xSemaphoreGive(_fileMutex);
            state.mutexHeld = false;
        }
        
        // Reset state
        state.reset();
    }
}

void WebServerManager::handleFirmwareUpload() {
    HTTPUpload& upload = server->upload();

    static bool updateStarted = false;
    static size_t totalSize = 0;
    static const size_t MAX_FIRMWARE_SIZE = 3 * 1024 * 1024; // 3MB limit

    _logger.debug("Firmware upload status: " + String(upload.status) + ", size: " + String(upload.currentSize));

    if (upload.status == UPLOAD_FILE_START) {
        _logger.info("Starting firmware upload: " + upload.filename);

        _firmwareUpdateSuccess = false;
        _firmwareUpdateError[0] = '\0';
        updateStarted = false;
        totalSize = 0;

        if (upload.filename != "discovery_drive.ino.bin") {
            _logger.error("Invalid firmware filename: " + upload.filename);
            safeCopy(_firmwareUpdateError, "Invalid filename. Please upload discovery_drive.ino.bin", sizeof(_firmwareUpdateError));
            return;
        }

    } else if (upload.status == UPLOAD_FILE_WRITE) {
        // Skip writing if we already have an error
        if (_firmwareUpdateError[0] != '\0') return;

        // Check size limit
        if (totalSize + upload.currentSize > MAX_FIRMWARE_SIZE) {
            _logger.error("Firmware too large - " + String(totalSize + upload.currentSize) + " bytes exceeds " + String(MAX_FIRMWARE_SIZE) + " byte limit");
            safeCopy(_firmwareUpdateError, "Firmware file too large. Maximum size is 3MB.", sizeof(_firmwareUpdateError));
            if (updateStarted) {
                Update.abort();
                updateStarted = false;
            }
            return;
        }

        if (!updateStarted) {
            _logger.debug("Starting firmware update");
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
                _logger.error("Cannot start firmware update");
                safeCopy(_firmwareUpdateError, "Cannot start firmware update. Not enough space or flash error.", sizeof(_firmwareUpdateError));
                return;
            }
            updateStarted = true;
        }

        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            _logger.error("Firmware write failed");
            safeCopy(_firmwareUpdateError, "Firmware write failed during upload.", sizeof(_firmwareUpdateError));
            Update.abort();
            updateStarted = false;
            return;
        }

        totalSize += upload.currentSize;
        _logger.debug("Firmware written: " + String(upload.currentSize) + " bytes (total: " + String(totalSize) + ")");

    } else if (upload.status == UPLOAD_FILE_END) {
        if (updateStarted) {
            if (Update.end(true)) {
                _logger.info("Firmware update completed: " + String(totalSize) + " bytes");
                _firmwareUpdateSuccess = true;
            } else {
                _logger.error("Firmware update finalization failed");
                safeCopy(_firmwareUpdateError, "Firmware update failed during finalization. Current firmware unchanged.", sizeof(_firmwareUpdateError));
            }
        } else if (_firmwareUpdateError[0] == '\0') {
            safeCopy(_firmwareUpdateError, "Firmware update was not started. Upload may have been invalid.", sizeof(_firmwareUpdateError));
        }
        updateStarted = false;

    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        _logger.debug("Firmware upload aborted");
        safeCopy(_firmwareUpdateError, "Firmware upload was aborted.", sizeof(_firmwareUpdateError));
        if (updateStarted) {
            Update.abort();
            updateStarted = false;
        }
    }
}

bool WebServerManager::updateFirmware(uint8_t* firmwareData, size_t firmwareSize) {
    _logger.info("Updating firmware: " + String(firmwareSize) + " bytes");

    if (!Update.begin(firmwareSize)) {
        _logger.error("Not enough space for firmware update");
        return false;
    }

    size_t written = Update.write(firmwareData, firmwareSize);
    if (written != firmwareSize) {
        _logger.error("Firmware write failed: " + String(written) + "/" + String(firmwareSize) + " bytes");
        Update.abort();
        return false;
    }

    if (!Update.end(true)) {
        _logger.error("Firmware update failed");
        return false;
    }

    _logger.info("Firmware update successful");
    return true;
}

bool WebServerManager::isValidUpdateFile(const String& filename) {
    if (filename.length() == 0) return false;

    return filename == "index.html" ||
           filename == "script.js" ||
           filename == "styles.css" ||
           filename == "Logo-Circle-Cream.png";
}

void WebServerManager::sendOTAResponse(const String& message, bool success) {
    String html = "<!DOCTYPE html><html><head><title>OTA Update</title>";
    html += "<style>body{font-family:Arial;margin:40px;text-align:center;}";
    html += ".message{padding:20px;margin:20px auto;max-width:500px;border-radius:5px;}";
    html += ".success{background:#d4edda;color:#155724;border:1px solid #c3e6cb;}";
    html += ".error{background:#f8d7da;color:#721c24;border:1px solid #f5c6cb;}";
    html += "button{background:#4CAF50;color:white;padding:10px 20px;border:none;border-radius:5px;cursor:pointer;font-size:16px;margin-top:20px;}";
    html += "button:hover{background:#45a049;}</style></head><body>";
    html += "<h1>OTA Update</h1>";
    html += "<div class='message " + String(success ? "success" : "error") + "'>";
    html += message;
    html += "</div>";
    if (!success) {
        html += "<button onclick=\"window.location.href='/ota'\">Try Again</button>";
        html += "<button onclick=\"window.location.href='/'\">Back to Main</button>";
    }
    html += "</body></html>";

    server->send(success ? 200 : 400, "text/html", html);
}

// =============================================================================
// UTILITY METHODS
// =============================================================================

// Returns true if string represents a valid float and stores result in outValue
// Safe for situations where text is accidentally input as a number
bool WebServerManager::parseFloat(const String& str, float& outValue) {
    if (str.length() == 0) return false;
    
    const char* cstr = str.c_str();
    char* endPtr = nullptr;
    float value = strtof(cstr, &endPtr);
    
    // Check if entire string was consumed (valid number)
    // and that we actually parsed something
    if (endPtr == cstr || *endPtr != '\0') {
        return false;  // Invalid: no conversion or trailing garbage
    }
    
    if (isnan(value) || isinf(value)) {
        return false;  // Invalid: NaN or infinity
    }
    
    outValue = value;
    return true;
}



String WebServerManager::createRestartResponse(const String& title, const String& message) {
    return "<!DOCTYPE html>\n"
           "<html>\n"
           "<head>\n"
           "<title>" + title + "</title>\n"
           "<script>\n"
           "  function enableButton() {\n"
           "    var countdown = 10;\n"
           "    var button = document.getElementById('backButton');\n"
           "    var timer = setInterval(function() {\n"
           "      button.innerHTML = 'Go Back (' + countdown + ')';\n"
           "      countdown--;\n"
           "      if (countdown < 0) {\n"
           "        clearInterval(timer);\n"
           "        button.innerHTML = 'Go Back';\n"
           "        button.disabled = false;\n"
           "      }\n"
           "    }, 1000);\n"
           "  }\n"
           "</script>\n"
           "</head>\n"
           "<body onload=\"enableButton()\">\n"
           "<h1>" + message + "</h1>\n"
           "<button id='backButton' onclick=\"window.location.href='http://' + window.location.hostname + ':" + 
           String(preferences.getInt("http_port", 80)) + "'\" disabled>Go Back (10)</button>\n"
           "</body>\n"
           "</html>\n";
}

void WebServerManager::handleStaticFile(const String& filePath, const String& contentType) {
    if (xSemaphoreTake(_fileMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        File file = LittleFS.open(filePath, "r");
        if (file) {
            server->streamFile(file, contentType);
            file.close();
        } else {
            server->send(404, "text/plain", "File not found: " + filePath);
        }
        xSemaphoreGive(_fileMutex);
    } else {
        server->send(503, "text/plain", "Server busy");
    }
}

String WebServerManager::loadIndexHTML() {
    if (xSemaphoreTake(_fileMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        File file = LittleFS.open("/index.html", "r");
        if (!file) {
            xSemaphoreGive(_fileMutex);
            return "";
        }
        
        size_t fileSize = file.size();
        String html;
        html.reserve(fileSize + 500);
        
        const size_t bufSize = 512;
        char buf[bufSize];
        while (file.available()) {
            size_t bytesRead = file.readBytes(buf, bufSize - 1);
            buf[bytesRead] = 0;
            html += buf;
        }

        file.close();
        xSemaphoreGive(_fileMutex);
        return html;
    }
    return "";
}

void WebServerManager::verifyUploadedFile(const String& filename, size_t expectedSize) {
    if (xSemaphoreTake(_fileMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        File verifyFile = LittleFS.open("/" + filename, "r");
        if (verifyFile) {
            size_t fileSize = verifyFile.size();
            verifyFile.close();
            _logger.debug("File verification - size on disk: " + String(fileSize) + " bytes");
            if (fileSize != expectedSize) {
                _logger.warn("File size mismatch - expected " + String(expectedSize) + ", got " + String(fileSize));
            }
        }
        xSemaphoreGive(_fileMutex);
    }
}

String WebServerManager::generateOTAUploadHTML() {
    String html = "<!DOCTYPE html><html><head><title>OTA Update</title>";
    html += "<style>body{font-family:Arial;margin:40px;text-align:center;}";
    html += ".upload-box{border:2px dashed #ccc;padding:40px;margin:20px auto;max-width:600px;}";
    html += ".upload-section{margin:30px 0;padding:20px;border:1px solid #ddd;border-radius:5px;}";
    html += "input[type=file]{margin:20px;}";
    html += "button{background:#4CAF50;color:white;padding:10px 20px;border:none;border-radius:5px;cursor:pointer;font-size:16px;margin:5px;}";
    html += "button:hover{background:#45a049;}";
    html += ".firmware-btn{background:#ff9800;}";
    html += ".firmware-btn:hover{background:#f57c00;}";
    html += ".warning{color:#d32f2f;font-weight:bold;margin:10px 0;}";
    html += ".progress-container{margin:20px 0;display:none;}";
    html += ".progress-bar{width:100%;height:30px;background-color:#f0f0f0;border-radius:15px;overflow:hidden;border:1px solid #ddd;}";
    html += ".progress-fill{height:100%;background:linear-gradient(90deg,#4CAF50 0%,#45a049 100%);width:0%;transition:width 0.3s ease;border-radius:15px;position:relative;}";
    html += ".progress-text{position:absolute;width:100%;text-align:center;line-height:30px;color:white;font-weight:bold;font-size:14px;}";
    html += ".upload-status{margin:10px 0;font-weight:bold;}";
    html += ".status-uploading{color:#ff9800;}";
    html += ".status-success{color:#4CAF50;}";
    html += ".status-error{color:#d32f2f;}";
    html += "</style>";
    
    html += generateUploadJavaScript();
    
    html += "</head><body>";
    html += "<h1>OTA Update</h1>";
    
    // Firmware Update Section
    html += "<div class='upload-section'>";
    html += "<h2>Firmware Update</h2>";
    html += "<p>Upload discovery_drive.ino.bin to update the ESP32 firmware</p>";
    html += "<div class='warning'>Device will restart after firmware update</div>";
    html += "<div class='upload-box'>";
    html += "<form id='firmwareForm' onsubmit='return uploadFile(\"firmwareForm\", \"/firmware\", false);'>";
    html += "<input type='file' name='firmware' accept='.bin' required>";
    html += generateProgressBarHTML();
    html += "<br><button type='submit' class='firmware-btn'>Upload Firmware</button>";
    html += "</form></div></div>";
    
    // File Update Section
    html += "<div class='upload-section'>";
    html += "<h2>Web Assets Update</h2>";
    html += "<p>Upload individual files to update web interface</p>";
    html += "<p><strong>Allowed files:</strong> index.html, script.js, styles.css, Logo-Circle-Cream.png</p>";
    html += "<div class='upload-box'>";
    html += "<form id='fileForm' onsubmit='return uploadFile(\"fileForm\", \"/fileupdate\", true);'>";
    html += "<input type='file' name='webfile' accept='.html,.css,.js,.png' required>";
    html += generateProgressBarHTML();
    html += "<br><button type='submit'>Upload File</button>";
    html += "</form></div></div>";
    
    html += "<p><strong>Note:</strong> Maximum file size is 3MB per upload.</p>";
    html += "<p><a href='/'>Back to Main Page</a></p>";
    html += "</body></html>";
    
    return html;
}

String WebServerManager::generateUploadJavaScript() {
    return "<script>"
           "function uploadFile(formId, url, isRegular) {"
           "  var form = document.getElementById(formId);"
           "  var fileInput = form.querySelector('input[type=file]');"
           "  var progressContainer = form.querySelector('.progress-container');"
           "  var progressFill = form.querySelector('.progress-fill');"
           "  var progressText = form.querySelector('.progress-text');"
           "  var statusDiv = form.querySelector('.upload-status');"
           "  var submitBtn = form.querySelector('button[type=submit]');"
           "  "
           "  if (!fileInput.files[0]) {"
           "    alert('Please select a file');"
           "    return false;"
           "  }"
           "  "
           "  var file = fileInput.files[0];"
           "  var maxSize = 3 * 1024 * 1024;"
           "  "
           "  if (file.size > maxSize) {"
           "    alert('File too large. Maximum size is 3MB.');"
           "    return false;"
           "  }"
           "  "
           "  if (isRegular) {"
           "    var allowed = ['index.html','script.js','styles.css','Logo-Circle-Cream.png'];"
           "    if (allowed.indexOf(file.name) === -1) {"
           "      alert('Invalid filename: ' + file.name + '. Allowed: ' + allowed.join(', '));"
           "      return false;"
           "    }"
           "  } else {"
           "    if (file.name !== 'discovery_drive.ino.bin') {"
           "      alert('Invalid filename: ' + file.name + '. Please upload discovery_drive.ino.bin');"
           "      return false;"
           "    }"
           "  }"
           "  "
           "  progressContainer.style.display = 'block';"
           "  statusDiv.innerHTML = 'Uploading...';"
           "  statusDiv.className = 'upload-status status-uploading';"
           "  submitBtn.disabled = true;"
           "  submitBtn.innerHTML = 'Uploading...';"
           "  "
           "  var formData = new FormData(form);"
           "  var xhr = new XMLHttpRequest();"
           "  "
           "  xhr.upload.addEventListener('progress', function(e) {"
           "    if (e.lengthComputable) {"
           "      var percentComplete = (e.loaded / e.total) * 100;"
           "      progressFill.style.width = percentComplete + '%';"
           "      progressText.innerHTML = Math.round(percentComplete) + '%';"
           "      var mbLoaded = (e.loaded / 1024 / 1024).toFixed(1);"
           "      var mbTotal = (e.total / 1024 / 1024).toFixed(1);"
           "      statusDiv.innerHTML = 'Uploading: ' + mbLoaded + ' / ' + mbTotal + ' MB';"
           "    }"
           "  });"
           "  "
           "  xhr.onload = function() {"
           "    if (xhr.status === 200) {"
           "      progressFill.style.width = '100%';"
           "      progressText.innerHTML = '100%';"
           "      statusDiv.innerHTML = 'Upload successful!';"
           "      statusDiv.className = 'upload-status status-success';"
           "      "
           "      if (!isRegular) {"
           "        statusDiv.innerHTML = 'Firmware update successful! Restarting device...';"
           "        setTimeout(function() { window.location.href = '/'; }, 5000);"
           "      } else {"
           "        setTimeout(function() { window.location.href = '/ota'; }, 2000);"
           "      }"
           "    } else {"
           "      statusDiv.innerHTML = 'Upload failed. Please try again.';"
           "      statusDiv.className = 'upload-status status-error';"
           "      submitBtn.disabled = false;"
           "      submitBtn.innerHTML = isRegular ? 'Upload File' : 'Upload Firmware';"
           "    }"
           "  };"
           "  "
           "  xhr.onerror = function() {"
           "    statusDiv.innerHTML = 'Upload failed. Please check your connection.';"
           "    statusDiv.className = 'upload-status status-error';"
           "    submitBtn.disabled = false;"
           "    submitBtn.innerHTML = isRegular ? 'Upload File' : 'Upload Firmware';"
           "  };"
           "  "
           "  xhr.open('POST', url);"
           "  xhr.send(formData);"
           "  return false;"
           "}"
           "</script>";
}

String WebServerManager::generateProgressBarHTML() {
    return "<div class='progress-container'>"
           "<div class='progress-bar'>"
           "<div class='progress-fill'>"
           "<div class='progress-text'>0%</div>"
           "</div></div>"
           "<div class='upload-status'></div>"
           "</div>";
}

// =============================================================================
// AUTHENTICATION METHODS
// =============================================================================

const char* WebServerManager::getLoginUser() {
    // Returns pointer to internal buffer — only called from web server task (single-threaded access)
    static char buf[33];
    if (_loginUserMutex != NULL && xSemaphoreTake(_loginUserMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        safeCopy(buf, _loginUser, sizeof(buf));
        xSemaphoreGive(_loginUserMutex);
    }
    return buf;
}

void WebServerManager::setLoginUser(const char* loginUser) {
    if (_loginUserMutex != NULL && xSemaphoreTake(_loginUserMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        safeCopy(_loginUser, loginUser, sizeof(_loginUser));
        xSemaphoreGive(_loginUserMutex);
    }
}

const char* WebServerManager::getLoginPassword() {
    static char buf[65];
    if (_loginUserMutex != NULL && xSemaphoreTake(_loginUserMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        safeCopy(buf, _loginPassword, sizeof(buf));
        xSemaphoreGive(_loginUserMutex);
    }
    return buf;
}

void WebServerManager::setLoginPassword(const char* loginPassword) {
    if (_loginUserMutex != NULL && xSemaphoreTake(_loginUserMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        safeCopy(_loginPassword, loginPassword, sizeof(_loginPassword));
        xSemaphoreGive(_loginUserMutex);
    }
}

