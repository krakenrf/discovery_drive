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
 
#ifndef WEB_SERVER_H
#define WEB_SERVER_H

// System includes
#include <Arduino.h>
#include <WebServer.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <atomic>
#include <Update.h>

// Custom includes
#include "motor_controller.h"
#include "ina219_manager.h"
#include "stellarium_poller.h"
#include "serial_manager.h"
#include "wifi_manager.h"
#include "rotctl_wifi.h"
#include "logger.h"
#include "weather_poller.h"

class WebServerManager {
public:
    // Constructor
    WebServerManager(Preferences& prefs, MotorSensorController& motorController, INA219Manager& ina219Manager, 
                StellariumPoller& stellariumPoller, WeatherPoller& weatherPoller, SerialManager& serialManager, 
                WiFiManager& wifiManager, RotctlWifi& rotctlWifi, Logger& logger);

    // Core functionality
    void begin();
    void setupRoutes();
    
    // Content and response methods
    String createRestartResponse(const String& title, const String& message);
    void handleStaticFile(const String& filePath, const String& contentType);
    String loadIndexHTML();

    // Authentication getters and setters
    String getLoginUser();
    void setLoginUser(String loginUser);
    String getLoginPassword();
    void setLoginPassword(String loginPassword);

    // Public members
    WebServer* server;
    String wifi_ssid = "";
    String wifi_password = "";

private:
    // Dependencies
    Preferences& preferences;
    MotorSensorController& msc;
    INA219Manager& ina219Manager;
    StellariumPoller& stellariumPoller;
    WiFiManager& wifiManager;
    SerialManager& serialManager;
    RotctlWifi& rotctlWifi;
    Logger& _logger;
    WeatherPoller& weatherPoller;

    // Authentication configuration
    bool _loginRequired = true;
    String _loginUser = "";
    String _loginPassword = "";

    // Thread synchronization
    SemaphoreHandle_t _fileMutex = NULL;
    SemaphoreHandle_t _loginUserMutex = NULL;

    // Route setup methods
    void setupStaticRoutes();
    void setupMainPageRoutes();
    void setupSystemControlRoutes();
    void setupMotorControlRoutes();
    void setupConfigurationRoutes();
    void setupAPIRoutes();
    void setupDebugRoutes();
    void setupFileUploadRoute();
    void setupFirmwareUploadRoute();

    // OTA update methods
    void handleOTAUpload();
    void handleFileUpload();
    void handleFirmwareUpload();
    bool updateFirmware(uint8_t* firmwareData, size_t firmwareSize);
    bool isValidUpdateFile(const String& filename);
    void sendOTAResponse(const String& message, bool success = true);
    
    // Utility methods for file handling and HTML generation
    void verifyUploadedFile(const String& filename, size_t expectedSize);
    String generateOTAUploadHTML();
    String generateUploadJavaScript();
    String generateProgressBarHTML();
};

#endif // WEB_SERVER_H