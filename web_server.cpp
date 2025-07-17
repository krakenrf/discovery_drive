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

// =============================================================================
// CONSTRUCTOR AND INITIALIZATION
// =============================================================================

WebServerManager::WebServerManager(Preferences& prefs, MotorSensorController& motorController, INA219Manager& ina219Manager, 
                                   StellariumPoller& stellariumPoller, SerialManager& serialManager, WiFiManager& wifiManager, 
                                   RotctlWifi& rotctlWifi, Logger& logger)
    : preferences(prefs), msc(motorController), ina219Manager(ina219Manager), stellariumPoller(stellariumPoller),
      serialManager(serialManager), wifiManager(wifiManager), rotctlWifi(rotctlWifi), _logger(logger) {
    
    _fileMutex = xSemaphoreCreateMutex();
    _loginUserMutex = xSemaphoreCreateMutex();
}

void WebServerManager::begin() {
    server = new WebServer(preferences.getInt("http_port", 80));

    wifi_ssid = preferences.getString("wifi_ssid", "");
    wifi_password = preferences.getString("wifi_password", "");
    _loginUser = preferences.getString("loginUser", "");
    _loginPassword = preferences.getString("loginPassword", "");

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
    server->on("/styles.css", HTTP_GET, [this]() {
        handleStaticFile("/styles.css", "text/css");
    });

    server->on("/script.js", HTTP_GET, [this]() {
        handleStaticFile("/script.js", "application/javascript");
    });

    server->on("/Logo-Circle-Cream.png", HTTP_GET, [this]() {
        handleStaticFile("/Logo-Circle-Cream.png", "image/png");
    });
}

void WebServerManager::setupMainPageRoutes() {
    // Main page route with authentication
    server->on("/", HTTP_GET, [this]() {
        String loginUser = getLoginUser();
        String loginPassword = getLoginPassword();

        if (_loginRequired && loginUser.length() != 0 && loginPassword.length() != 0) {
            if (!server->authenticate(loginUser.c_str(), loginPassword.c_str())) {
                return server->requestAuthentication();
            }
        }

        String html = loadIndexHTML();
        if (html == "") {
            server->send(500, "text/plain", "Failed to load HTML template");
            return;
        }

        // Replace template variables
        html.replace("%var_calmode_checked%", msc.calMode ? "checked" : "");
        html.replace("%var_singleMotorMode_checked%", msc.singleMotorMode ? "checked" : "");
        
        bool stellariumOn = preferences.getBool("stellariumOn", false);
        html.replace("%var_stellariumOn_checked%", stellariumOn ? "checked" : "");

        server->send(200, "text/html", html);
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
}

void WebServerManager::setupMotorControlRoutes() {
    server->on("/update_variable", HTTP_POST, [this]() {
        if (server->hasArg("new_setpoint_el")) {
            String update_var = server->arg("new_setpoint_el");
            if (update_var.length() != 0) {
                float el = update_var.toFloat();
                if (el >= 0 && el <= 90) {
                    msc.setSetPointEl(el);
                }
            }
        }

        if (server->hasArg("new_setpoint_az")) {
            String update_var = server->arg("new_setpoint_az");
            if (update_var.length() != 0) {
                float az = update_var.toFloat();
                az = fmod(az, 360.0);
                if (az < 0) {
                    az += 360.0;
                }
                msc.setSetPointAz(az);
            }
        }
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
                msc.calMoveMotor(value, "AZ");
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
                msc.calMoveMotor(value, "EL");
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
}

void WebServerManager::setupConfigurationRoutes() {
    server->on("/setPassword", HTTP_POST, [this]() {
        if (server->hasArg("loginUser")) {
            String loginUser = server->arg("loginUser");
            setLoginUser(loginUser);
            preferences.putString("loginUser", loginUser);
        }

        if (server->hasArg("loginPassword")) {
            String loginPassword = server->arg("loginPassword");
            setLoginPassword(loginPassword);
            preferences.putString("loginPassword", loginPassword);
        }

        server->send(204);
    });

    server->on("/setWiFi", HTTP_POST, [this]() {
        bool hotspotMode = server->hasArg("hotspot");

        if (hotspotMode) {
            wifi_ssid = "";
            wifi_password = "";
        } else if (server->hasArg("ssid") && server->hasArg("password")) {
            wifi_ssid = server->arg("ssid");
            wifi_password = server->arg("password");
        }

        if ((wifi_ssid.length() != 0 && wifi_password.length() != 0) || hotspotMode) {
            preferences.putString("wifi_ssid", wifi_ssid);
            preferences.putString("wifi_password", wifi_password);
            String htmlResponse = createRestartResponse("WiFi Credentials Updated!", "WiFi Credentials Updated! Restarting...");
            server->send(200, "text/html", htmlResponse);
            delay(1000);
            ESP.restart();
        } else {
            server->send(204);
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
                msc.max_dual_motor_az_speed = msc.convertPercentageToSpeed(azSpeedValue.toFloat());
                preferences.putInt("maxDMAzSpeed", msc.max_dual_motor_az_speed);
            }
        }

        if (server->hasArg("maxDualMotorElSpeed")) {
            String elSpeedValue = server->arg("maxDualMotorElSpeed");
            if (elSpeedValue.length() > 0) {
                msc.max_dual_motor_el_speed = msc.convertPercentageToSpeed(elSpeedValue.toFloat());
                preferences.putInt("maxDMElSpeed", msc.max_dual_motor_el_speed);
            }
        }
        server->send(204);
    });

    server->on("/setSingleMotorMaxSpeed", HTTP_POST, [this]() {
        if (server->hasArg("maxSingleMotorAzSpeed")) {
            String azSpeedValue = server->arg("maxSingleMotorAzSpeed");
            if (azSpeedValue.length() > 0) {
                msc.max_single_motor_az_speed = msc.convertPercentageToSpeed(azSpeedValue.toFloat());
                preferences.putInt("maxSMAzSpeed", msc.max_single_motor_az_speed);
            }
        }

        if (server->hasArg("maxSingleMotorElSpeed")) {
            String elSpeedValue = server->arg("maxSingleMotorElSpeed");
            if (elSpeedValue.length() > 0) {
                msc.max_single_motor_el_speed = msc.convertPercentageToSpeed(elSpeedValue.toFloat());
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

        setIntParam("P_el", [this](int v) { msc.setPEl(v); }, -1000, 1000);
        setIntParam("P_az", [this](int v) { msc.setPAz(v); }, -1000, 1000);
        setIntParam("MIN_EL_SPEED", [this](int v) { msc.setMinElSpeed(v); }, 0, 255);
        setIntParam("MIN_AZ_SPEED", [this](int v) { msc.setMinAzSpeed(v); }, 0, 255);
        setIntParam("MAX_FAULT_POWER", [this](int v) { msc.setMaxPowerBeforeFault(v); }, 1, 25);
        
        setFloatParam("MIN_AZ_TOLERANCE", [this](float v) { msc.setMinAzTolerance(v); }, 0.1f, 10.0f);
        setFloatParam("MIN_EL_TOLERANCE", [this](float v) { msc.setMinElTolerance(v); }, 0.1f, 10.0f);
        
        if (updated) {
            _logger.info("Advanced parameters updated via web interface");
        }
        
        server->send(204);
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

    server->on("/variable", HTTP_GET, [this]() {
        static DynamicJsonDocument doc(2048);
        doc.clear();

        // Motor and control data
        doc["correctedAngle_el"] = String(msc.getCorrectedAngleEl());
        doc["correctedAngle_az"] = String(msc.getCorrectedAngleAz());
        doc["setpoint_az"] = String(msc.getSetPointAz());
        doc["setpoint_el"] = String(msc.getSetPointEl());
        doc["setPointState_az"] = String((int)msc.setPointState_az);
        doc["setPointState_el"] = String((int)msc.setPointState_el);
        doc["error_az"] = String(msc.getErrorAz());
        doc["error_el"] = String(msc.getErrorEl());
        doc["el_startAngle"] = String(msc.getElStartAngle());
        doc["needs_unwind"] = String(msc.needs_unwind);
        
        // Status flags
        doc["calMode"] = msc.calMode ? "ON" : "OFF";
        doc["i2cErrorFlag_az"] = String(msc.i2cErrorFlag_az);
        doc["i2cErrorFlag_el"] = String(msc.i2cErrorFlag_el);
        doc["faultTripped"] = String((int)msc.global_fault);
        doc["badAngleFlag"] = String((int)msc.badAngleFlag);
        doc["magnetFault"] = String((int)msc.magnetFault);
        doc["isAzMotorLatched"] = String(msc._isAzMotorLatched);
        doc["isElMotorLatched"] = String(msc._isElMotorLatched);

        // Configuration data
        doc["http_port"] = String(preferences.getInt("http_port", 80));
        doc["rotctl_port"] = String(preferences.getInt("rotctl_port", 4533));
        doc["maxDualMotorAzSpeed"] = String(msc.convertSpeedToPercentage((float)msc.max_dual_motor_az_speed));
        doc["maxDualMotorElSpeed"] = String(msc.convertSpeedToPercentage((float)msc.max_dual_motor_el_speed));
        doc["maxSingleMotorAzSpeed"] = String(msc.convertSpeedToPercentage((float)msc.max_single_motor_az_speed));
        doc["maxSingleMotorElSpeed"] = String(msc.convertSpeedToPercentage((float)msc.max_single_motor_el_speed));
        doc["wifissid"] = preferences.getString("wifi_ssid", "discoverydish_HOTSPOT");
        doc["loginUser"] = preferences.getString("loginUser", "");
        
        String passwordStatus = (preferences.getString("loginUser", "").length() != 0 && 
                               preferences.getString("loginPassword", "").length() != 0 && 
                               _loginRequired) ? "True" : "False";
        doc["passwordStatus"] = passwordStatus;
        doc["serialActive"] = String(serialManager.serialActive);

        // Mode indicators
        doc["singleMotorModeText"] = msc.singleMotorMode ? "ON" : "OFF";
        doc["stellariumPollingOn"] = preferences.getBool("stellariumOn", false) ? "ON" : "OFF";

        // Stellarium data
        doc["stellariumServerIPText"] = preferences.getString("stelServIP", "NO IP SET");
        doc["stellariumServerPortText"] = preferences.getString("stelServPort", "8090");
        doc["stellariumConnActive"] = stellariumPoller.getStellariumConnActive() ? "Connected" : "Disconnected";

        // Advanced parameters
        doc["toleranceAz"] = String(msc.getMinAzTolerance());
        doc["toleranceEl"] = String(msc.getMinElTolerance());
        doc["P_el"] = String(msc.getPEl());
        doc["P_az"] = String(msc.getPAz());
        doc["MIN_EL_SPEED"] = String(msc.getMinElSpeed());
        doc["MIN_AZ_SPEED"] = String(msc.getMinAzSpeed());
        doc["MIN_AZ_TOLERANCE"] = String(msc.getMinAzTolerance());
        doc["MIN_EL_TOLERANCE"] = String(msc.getMinElTolerance());
        doc["MAX_FAULT_POWER"] = String(msc.getMaxPowerBeforeFault());

        // Power and connectivity data
        doc["inputVoltage"] = String(ina219Manager.getLoadVoltage());
        doc["currentDraw"] = String(ina219Manager.getCurrent() / 1000);
        doc["rotatorPowerDraw"] = String(ina219Manager.getPower());

        int rssi = wifiManager.getRSSI();
        doc["rssi"] = String(rssi);
        doc["level"] = wifiManager.getSignalStrengthLevel(rssi);
        doc["ip_addr"] = wifiManager.ip_addr;
        doc["rotctl_client_ip"] = rotctlWifi.getRotctlClientIP();
        doc["bssid"] = wifiManager.getCurrentBSSID();
        doc["wifi_channel"] = wifiManager.getCurrentWiFiChannel();

        // Logging data
        doc["newLogMessages"] = _logger.getNewLogMessages();
        doc["currentDebugLevel"] = _logger.getDebugLevel();
        doc["serialOutputDisabled"] = _logger.getSerialOutputDisabled();

        String json;
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
    String loginUser = getLoginUser();
    String loginPassword = getLoginPassword();

    if (_loginRequired && loginUser.length() != 0 && loginPassword.length() != 0) {
        if (!server->authenticate(loginUser.c_str(), loginPassword.c_str())) {
            return server->requestAuthentication();
        }
    }

    String html = generateOTAUploadHTML();
    server->send(200, "text/html", html);
}

void WebServerManager::handleFileUpload() {
    HTTPUpload& upload = server->upload();
    
    static String uploadFilename = "";
    static File uploadFile;
    static bool uploadSuccess = false;
    static size_t totalBytesWritten = 0;
    static const size_t MAX_UPLOAD_SIZE = 3 * 1024 * 1024; // 3MB limit

    _logger.debug("Upload status: " + String(upload.status) + ", filename: " + upload.filename + ", size: " + String(upload.currentSize));

    if (upload.status == UPLOAD_FILE_START) {
        _logger.info("Starting upload: " + String(upload.filename.c_str()));
        
        uploadFilename = upload.filename;
        uploadSuccess = false;
        totalBytesWritten = 0;
        
        if (!isValidUpdateFile(upload.filename)) {
            _logger.error("Invalid file type");
            return;
        }
        
        String filepath = "/" + upload.filename;
        
        if (xSemaphoreTake(_fileMutex, portMAX_DELAY) == pdTRUE) {
            uploadFile = LittleFS.open(filepath, "w");
            if (uploadFile) {
                _logger.debug("File opened for writing: " + filepath);
                uploadSuccess = true;
            } else {
                _logger.error("Cannot open file: " + filepath);
                xSemaphoreGive(_fileMutex);
            }
        } else {
            _logger.error("Cannot take file mutex");
        }
        
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        _logger.debug("Writing " + String(upload.currentSize) + " bytes (total so far: " + String(totalBytesWritten + upload.currentSize) + " bytes)");

        // Check file size limit
        if (totalBytesWritten + upload.currentSize > MAX_UPLOAD_SIZE) {
            _logger.error("File too large - " + String(totalBytesWritten + upload.currentSize) + " bytes exceeds " + String(MAX_UPLOAD_SIZE) + " byte limit");
            uploadSuccess = false;
            if (uploadFile) {
                uploadFile.close();
                uploadFile = File();
            }
            return;
        }
        
        if (uploadFile && uploadSuccess && upload.currentSize > 0) {
            size_t written = uploadFile.write(upload.buf, upload.currentSize);
            if (written != upload.currentSize) {
                _logger.error("Write failed - expected " + String(upload.currentSize) + ", wrote " + String(written));
                uploadSuccess = false;
            } else {
                totalBytesWritten += written;
            }
        } else {
            _logger.error("File not ready for writing");
            uploadSuccess = false;
        }
        
    } else if (upload.status == UPLOAD_FILE_END) {
        _logger.debug("Upload finished: " + upload.filename + ", total: " + String(upload.totalSize) + " bytes (written: " + String(totalBytesWritten) + " bytes)");

        if (uploadFile) {
            uploadFile.close();
            _logger.debug("File closed");
        }
        
        xSemaphoreGive(_fileMutex);
        
        if (uploadSuccess && totalBytesWritten > 0) {
            _logger.info("File uploaded: " + uploadFilename + " (" + String(totalBytesWritten) + " bytes)");
            verifyUploadedFile(uploadFilename, totalBytesWritten);
        } else {
            _logger.error("Upload failed: " + uploadFilename);
        }
        
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        _logger.debug("Upload aborted");
        if (uploadFile) {
            uploadFile.close();
        }
        xSemaphoreGive(_fileMutex);
        uploadSuccess = false;
        totalBytesWritten = 0;
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

        if (!upload.filename.endsWith(".bin")) {
            _logger.error("Invalid firmware file type");
            return;
        }
        
        updateStarted = false;
        totalSize = 0;
        
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        // Check size limit
        if (totalSize + upload.currentSize > MAX_FIRMWARE_SIZE) {
            _logger.error("Firmware too large - " + String(totalSize + upload.currentSize) + " bytes exceeds " + String(MAX_FIRMWARE_SIZE) + " byte limit");
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
                return;
            }
            updateStarted = true;
        }
        
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            _logger.error("Firmware write failed");
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
            } else {
                _logger.error("Firmware update failed");
            }
        }
        updateStarted = false;
        
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
        _logger.debug("Firmware upload aborted");
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
    
    String lowercaseFilename = filename;
    lowercaseFilename.toLowerCase();
    
    return lowercaseFilename.endsWith(".html") || 
           lowercaseFilename.endsWith(".css") || 
           lowercaseFilename.endsWith(".js") || 
           lowercaseFilename.endsWith(".png") || 
           lowercaseFilename.endsWith(".jpg") || 
           lowercaseFilename.endsWith(".jpeg");
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
    if (xSemaphoreTake(_fileMutex, portMAX_DELAY) == pdTRUE) {
        File file = LittleFS.open(filePath, "r");
        if (file) {
            server->streamFile(file, contentType);
            file.close();
        } else {
            server->send(404, "text/plain", "File not found: " + filePath);
        }
        xSemaphoreGive(_fileMutex);
    }
}

String WebServerManager::loadIndexHTML() {
    if (xSemaphoreTake(_fileMutex, portMAX_DELAY) == pdTRUE) {
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
    if (xSemaphoreTake(_fileMutex, portMAX_DELAY) == pdTRUE) {
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
    html += "<p>Upload a .bin file to update the ESP32 firmware</p>";
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
    html += "<p><strong>Supported:</strong> .html, .css, .js, .png, .jpg, .jpeg</p>";
    html += "<div class='upload-box'>";
    html += "<form id='fileForm' onsubmit='return uploadFile(\"fileForm\", \"/fileupdate\", true);'>";
    html += "<input type='file' name='webfile' accept='.html,.css,.js,.png,.jpg,.jpeg' required>";
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

String WebServerManager::getLoginUser() {
    String result = "";
    if (_loginUserMutex != NULL && xSemaphoreTake(_loginUserMutex, portMAX_DELAY) == pdTRUE) {
        result = _loginUser;
        xSemaphoreGive(_loginUserMutex);
    }
    return result;
}

void WebServerManager::setLoginUser(String loginUser) {
    if (_loginUserMutex != NULL && xSemaphoreTake(_loginUserMutex, portMAX_DELAY) == pdTRUE) {
        _loginUser = loginUser;
        xSemaphoreGive(_loginUserMutex);
    }
}

String WebServerManager::getLoginPassword() {
    String result = "";
    if (_loginUserMutex != NULL && xSemaphoreTake(_loginUserMutex, portMAX_DELAY) == pdTRUE) {
        result = _loginPassword;
        xSemaphoreGive(_loginUserMutex);
    }
    return result;
}

void WebServerManager::setLoginPassword(String loginPassword) {
    if (_loginUserMutex != NULL && xSemaphoreTake(_loginUserMutex, portMAX_DELAY) == pdTRUE) {
        _loginPassword = loginPassword;
        xSemaphoreGive(_loginUserMutex);
    }
}