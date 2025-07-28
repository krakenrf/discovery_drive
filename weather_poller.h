/*
 * Firmware for the discovery-drive satellite dish rotator.
 * Weather Poller - Poll the WeatherAPI.com API for wind data and forecasts.
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

#ifndef WEATHER_POLLER_H
#define WEATHER_POLLER_H

// System includes
#include <Preferences.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <atomic>

// Custom includes
#include "logger.h"

struct WeatherData {
    float currentWindSpeed = 0.0;       // km/h
    float currentWindGust = 0.0;        // km/h 
    float currentWindDirection = 0.0;   // degrees
    String currentTime = "";
    
    // 3-hour forecast arrays
    float forecastWindSpeed[3] = {0.0, 0.0, 0.0};
    float forecastWindGust[3] = {0.0, 0.0, 0.0};
    float forecastWindDirection[3] = {0.0, 0.0, 0.0};
    String forecastTimes[3] = {"", "", ""};
    
    bool dataValid = false;
    String lastUpdateTime = "";
    String errorMessage = "";
};

struct WindSafetyData {
    bool emergencyStowActive = false;
    bool forecastStowActive = false;  // Keep this if you use it elsewhere
    float currentStowDirection = 0.0;
    String stowReason = "";
};

class WeatherPoller {
public:
    // Constructor
    WeatherPoller(Preferences& prefs, Logger& logger);

    // Core functionality
    void begin();
    void runWeatherLoop(bool wifiConnected);

    // Configuration methods
    bool setLocation(float latitude, float longitude);
    bool setApiKey(const String& apiKey);
    float getLatitude();
    float getLongitude();
    String getApiKey();
    bool isLocationConfigured();
    bool isApiKeyConfigured();
    bool isFullyConfigured();

    // Data access methods (thread-safe)
    WeatherData getWeatherData();
    bool isDataValid();
    String getLastError();
    unsigned long getLastUpdateTime();

    // Control methods
    void forceUpdate();
    bool isPollingEnabled();
    void setPollingEnabled(bool enabled);

    // Wind safety methods
    void setWindSafetyEnabled(bool enabled);
    bool isWindSafetyEnabled();
    void setWindSpeedThreshold(float threshold);
    float getWindSpeedThreshold();
    void setWindGustThreshold(float threshold);
    float getWindGustThreshold();
    void setWindBasedHomeEnabled(bool enabled);
    bool isWindBasedHomeEnabled();
    
    WindSafetyData getWindSafetyData();
    bool shouldActivateEmergencyStow();
    float calculateOptimalStowDirection(float windDirection);
    float getWindBasedHomePosition();

private:
    // Dependencies
    Preferences& _preferences;
    Logger& _logger;

    // Configuration
    std::atomic<float> _latitude{0.0};
    std::atomic<float> _longitude{0.0};
    std::atomic<bool> _pollingEnabled{true};
    String _apiKey = "";
    
    // Wind safety configuration
    std::atomic<bool> _windSafetyEnabled{false};
    std::atomic<float> _windSpeedThreshold{50.0};    // km/h
    std::atomic<float> _windGustThreshold{60.0};     // km/h
    std::atomic<bool> _windBasedHomeEnabled{false};
    
    // Timing constants
    static constexpr unsigned long POLL_INTERVAL_MS = 300000; // 5 minutes
    static constexpr unsigned long RETRY_INTERVAL_MS = 300000; // 5 minutes on error
    static constexpr unsigned long HTTP_TIMEOUT_MS = 15000;    // 15 seconds
    
    // State variables
    std::atomic<unsigned long> _lastPollTime{0};
    std::atomic<unsigned long> _lastSuccessTime{0};
    std::atomic<bool> _forceUpdate{false};
    
    // Thread synchronization
    SemaphoreHandle_t _weatherDataMutex = NULL;
    SemaphoreHandle_t _apiKeyMutex = NULL;
    SemaphoreHandle_t _windSafetyMutex = NULL;
    
    // Weather data storage
    WeatherData _weatherData;
    WindSafetyData _windSafetyData;
    
    // Core functionality helpers
    bool shouldPollWeather();
    bool pollWeatherData();
    bool processWeatherResponse(const String& payload);
    String buildApiUrl();
    
    // Data processing helpers
    bool extractCurrentWeather(JsonDocument& doc);
    bool extractForecastWeather(JsonDocument& doc);
    void clearWeatherData();
    void setErrorState(const String& error);
    
    // Wind safety helpers
    void updateWindSafetyStatus();
    bool checkCurrentWindConditions();
    bool checkForecastWindConditions();
    void setEmergencyStowState(bool active, const String& reason);
    
    // Utility methods
    float validateWindSpeed(float speed);
    float validateWindDirection(float direction);
    String formatWeatherApiTime(const String& apiTime);
    String getRelativeUpdateTime();
    int getCurrentHourFromTime(const String& timeStr);
    int getHourFromTimeString(const String& timeStr);
    bool isValidCoordinate(float lat, float lon);
    bool isValidApiKey(const String& key);
    float normalizeAngle(float angle);
};

#endif // WEATHER_POLLER_H