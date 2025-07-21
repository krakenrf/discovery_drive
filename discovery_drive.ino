/*
 * Firmware for the discovery-drive satellite dish rotator.
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

#include <Preferences.h>
#include <LittleFS.h> // Remember to use the LittleFS upload tool! https://randomnerdtutorials.com/arduino-ide-2-install-esp32-littlefs/#upload-files
#include "wifi_manager.h"
#include "motor_controller.h"
#include "web_server.h"
#include "ina219_manager.h"
#include "stellarium_poller.h"
#include "weather_poller.h"
#include "serial_manager.h"
#include "rotctl_wifi.h"
#include "logger.h"

#if CONFIG_FREERTOS_UNICORE
#define ARDUINO_RUNNING_CORE 0
#else
#define ARDUINO_RUNNING_CORE 1
#endif

// i2c PIN Settings
const int _SDA_PIN = 7;
const int _SCL_PIN = 6;
const int _SERIAL_BAUD = 19200;

// EEPROM Memory
Preferences preferences;

Logger logger(preferences);
WiFiManager wifiManager(preferences, logger);  // Pass preferences to constructor
INA219Manager ina219Manager(logger);
MotorSensorController motorSensorCtrl(preferences, ina219Manager, logger);
SerialManager serialManager(preferences, motorSensorCtrl, logger);
StellariumPoller stellariumPoller(preferences, motorSensorCtrl, logger);
WeatherPoller weatherPoller(preferences, logger);
RotctlWifi rotctlWifi(preferences, motorSensorCtrl, logger);
WebServerManager webServerManager(preferences, motorSensorCtrl, ina219Manager, stellariumPoller, weatherPoller, serialManager, wifiManager, rotctlWifi, logger);

void SafetyMonitor ( void *pvParameters );
void ReadPowerSensor( void *pvParameters );
void HandleWebRequests( void *pvParameters );
void ReadWiFi( void *pvParameters );
void PollStellarium( void *pvParameters );
void PollWeather( void *pvParameters );
void ProcessSerial( void *pvParameters );
void ControlMotors( void *pvParameters );

// The setup function runs once when you press reset or power on the board.
void setup() {

  // Setup Preferences
  if (!preferences.begin("dd", false)) {
    logger.error("Failed to initialize preferences");
  }

  // Initialize LittleFS for images and HTML file
  if (!LittleFS.begin(true)) {
    logger.error("Failed to mount LittleFS");
    return;
  }

  // Initialize serial
  Serial.begin(_SERIAL_BAUD);

  // Initialize i2c
  Wire.begin(_SDA_PIN, _SCL_PIN); //Start i2C  
	Wire.setClock(100000L); //fast clock // Signal integrity may be more important for this application
  Wire.setTimeOut(3000); // 3 second timeout for I2C operations

  // Init logger
  logger.begin();
  // Initialize the serial connection
  serialManager.begin();
  // Begin and connect to WiFi
  wifiManager.begin();
  // Initialize rotctl server
  rotctlWifi.begin();
  // Begin web server
  webServerManager.begin();
  // Initialize motor controller and home
  motorSensorCtrl.begin();
  // Initialize INA219 current sensor
  ina219Manager.begin();
  // Initialize weather poller
  weatherPoller.begin();

  xTaskCreatePinnedToCore(
    HandleWebRequests
    ,  "Web Server Task"
    ,  16384  // Stack size may need adjustment
    ,  NULL
    ,  1  // Priority (lower than LWIP tasks)
    ,  NULL
    ,  1   // Core to run on
  );

  xTaskCreatePinnedToCore(
    ReadPowerSensor
    ,  "Read Power Sensor"
    ,  2048
    ,  NULL
    ,  1  // Priority
    ,  NULL
    ,  1); // Run on core 0 (not timing critical)

  xTaskCreatePinnedToCore(
    ReadWiFi
    ,  "Read WiFi Input" // A name just for humans
    ,  8192        // The stack size can be checked by calling `uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);`
    ,  NULL // Task parameter which can modify the task behavior. This must be passed as pointer to void.
    ,  1  // Priority
    ,  NULL // Task handle is not used here - simply pass NULL
    ,  1);

  xTaskCreatePinnedToCore(
    PollStellarium
    ,  "Read Stellarium API" // A name just for humans
    ,  4096        // The stack size can be checked by calling `uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);`
    ,  NULL // Task parameter which can modify the task behavior. This must be passed as pointer to void.
    ,  1  // Priority
    ,  NULL // Task handle is not used here - simply pass NULL
    ,  1); // Run on core 0 (since polling is not timing critical)

  xTaskCreatePinnedToCore(
    PollWeather
    ,  "Poll Weather API" // A name just for humans
    ,  4096        // The stack size can be checked by calling `uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);`
    ,  NULL // Task parameter which can modify the task behavior. This must be passed as pointer to void.
    ,  1  // Priority
    ,  NULL // Task handle is not used here - simply pass NULL
    ,  1); // Run on core 0 (network polling is not timing critical)

  xTaskCreatePinnedToCore(
    ProcessSerial
    ,  "Process Serial Input" // A name just for humans
    ,  4096        // The stack size can be checked by calling `uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);`
    ,  NULL // Task parameter which can modify the task behavior. This must be passed as pointer to void.
    ,  1  // Priority
    ,  NULL // Task handle is not used here - simply pass NULL
    ,  1); // Watch out - putting this task on core 0 causes random serial garbling issues

  xTaskCreatePinnedToCore(
    SafetyMonitor
    ,  "Safety Monitor"
    ,  2048
    ,  NULL
    ,  2  // Max priority to safety monitor thread
    ,  NULL
    ,  0); // Run on core 1

  xTaskCreatePinnedToCore(
    ControlMotors
    ,  "Read ADC and Control motors" // A name just for humans
    ,  8192        // The stack size can be checked by calling `uxHighWaterMark = uxTaskGetStackHighWaterMark(NULL);`
    ,  NULL // Task parameter which can modify the task behavior. This must be passed as pointer to void.
    ,  2  // Priority
    ,  NULL // Task handle is not used here - simply pass NULL
    ,  1);
}

/*--------------------------------------------------*/
/*---------------------- Tasks ---------------------*/
/*--------------------------------------------------*/

// Read data from the INA219 current sensor
void ReadPowerSensor(void *pvParameters) {
  // Initialize the last wake time to the current tick count
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = 500 / portTICK_PERIOD_MS;

  for(;;) {
    ina219Manager.ReadData();
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

void SafetyMonitor(void *pvParameters) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  TickType_t xFrequency;

  for(;;) {
    motorSensorCtrl.runSafetyLoop();
    // Determine frequency based on fault status
    xFrequency = motorSensorCtrl.global_fault ? 
                pdMS_TO_TICKS(100) : pdMS_TO_TICKS(500);
    // Delay for the appropriate time
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

void HandleWebRequests(void *pvParameters) {
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = 100 / portTICK_PERIOD_MS;
  for(;;) {
    webServerManager.server->handleClient();
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

void ControlMotors(void *pvParameters){
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = 25 / portTICK_PERIOD_MS;
  for(;;)
  {
    motorSensorCtrl.runControlLoop();
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

void ProcessSerial(void *pvParameters){

  TickType_t xLastWakeTime = xTaskGetTickCount();
  TickType_t xFrequency;

  for(;;)
  {
    serialManager.runSerialLoop();
    xFrequency = serialManager.serialActive ? 
            pdMS_TO_TICKS(100) : pdMS_TO_TICKS(1000);
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

// Using Stellarium remote control API
void PollStellarium(void *pvParameters){
  TickType_t xLastWakeTime = xTaskGetTickCount();
  TickType_t xFrequency;

  for(;;)
  {
    stellariumPoller.runStellariumLoop(serialManager.serialActive, rotctlWifi.getRotctlClientIP(), wifiManager.wifiConnected);
    xFrequency = stellariumPoller.getStellariumOn() ? 
            pdMS_TO_TICKS(250) : pdMS_TO_TICKS(1000);
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
    // ------------------------------------------------------------------------
  }
}

// Poll weather data for wind information
void PollWeather(void *pvParameters){
  TickType_t xLastWakeTime = xTaskGetTickCount();
  TickType_t xFrequency;

  for(;;)
  {
    weatherPoller.runWeatherLoop(wifiManager.wifiConnected);
    xFrequency = weatherPoller.isPollingEnabled() ? 
            pdMS_TO_TICKS(5000) : pdMS_TO_TICKS(60000); // 5s when active, 60s when disabled
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
    // ------------------------------------------------------------------------
  }
}

// ROTCTL Protcol: https://manpages.ubuntu.com/manpages/xenial/man8/rotctld.8.html
void ReadWiFi(void *pvParameters){

  TickType_t xLastWakeTime = xTaskGetTickCount();
  TickType_t xFrequency;

  for(;;)
  {
  rotctlWifi.rotctlWifiLoop(serialManager.serialActive, stellariumPoller.getStellariumOn());
  xFrequency = rotctlWifi.isRotctlConnected() ? 
          pdMS_TO_TICKS(10) : pdMS_TO_TICKS(1000);
  vTaskDelayUntil(&xLastWakeTime, xFrequency);
  // ------------------------------------------------------------------------
  }
}

// Does nothing
void loop() {
  delay(10000);
}