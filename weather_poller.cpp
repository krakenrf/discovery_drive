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

#include "weather_poller.h"

// =============================================================================
// CONSTRUCTOR AND INITIALIZATION
// =============================================================================

WeatherPoller::WeatherPoller(Preferences& prefs, Logger& logger)
    : _preferences(prefs), _logger(logger) {
}

void WeatherPoller::begin() {
    // Create mutexes for thread-safe access
    _weatherDataMutex = xSemaphoreCreateMutex();
    _apiKeyMutex = xSemaphoreCreateMutex();
    _windSafetyMutex = xSemaphoreCreateMutex();
    
    // Load saved configuration
    _latitude = _preferences.getFloat("weather_lat", 0.0);
    _longitude = _preferences.getFloat("weather_lon", 0.0);
    _pollingEnabled = _preferences.getBool("weather_enabled", true);
    _apiKey = _preferences.getString("weather_api_key", "");
    
    // Load wind safety configuration
    _windSafetyEnabled = _preferences.getBool("wind_safety_enabled", false);
    _windSpeedThreshold = _preferences.getFloat("wind_speed_threshold", 50.0);
    _windGustThreshold = _preferences.getFloat("wind_gust_threshold", 60.0);
    _windBasedHomeEnabled = _preferences.getBool("wind_based_home", false);
    
    // Initialize weather data
    clearWeatherData();
    
    String configStatus = "Weather poller initialized - ";
    if (isFullyConfigured()) {
        configStatus += "Fully configured (Location: " + String(_latitude.load(), 6) + ", " + 
                       String(_longitude.load(), 6) + ", API key: SET)";
        
        // Force immediate update on first boot if fully configured
        if (_pollingEnabled) {
            _forceUpdate = true;
            _logger.info("Weather system configured - will fetch data immediately");
        }
    } else if (isLocationConfigured() && !isApiKeyConfigured()) {
        configStatus += "Location set but API key missing";
    } else if (!isLocationConfigured() && isApiKeyConfigured()) {
        configStatus += "API key set but location missing";
    } else {
        configStatus += "Not configured (missing location and API key)";
    }
    
    _logger.info(configStatus);
    
    // Log wind safety configuration
    if (_windSafetyEnabled) {
        _logger.info("Wind safety enabled - Speed threshold: " + String(_windSpeedThreshold.load(), 1) + 
                    " km/h, Gust threshold: " + String(_windGustThreshold.load(), 1) + " km/h");
    }
}

// =============================================================================
// CORE FUNCTIONALITY
// =============================================================================

void WeatherPoller::runWeatherLoop(bool wifiConnected) {
    // Check if we should poll weather data
    if (!shouldPollWeather()) {
        return;
    }
    
    if (!wifiConnected) {
        setErrorState("WiFi disconnected");
        return;
    }
    
    if (!isFullyConfigured()) {
        if (!isLocationConfigured()) {
            setErrorState("Location not configured");
        } else if (!isApiKeyConfigured()) {
            setErrorState("API key not configured");
        } else {
            setErrorState("Configuration incomplete");
        }
        return;
    }
    
    // Attempt to poll weather data
    _lastPollTime = millis();
    
    if (pollWeatherData()) {
        _lastSuccessTime = millis();
        updateWindSafetyStatus(); // Update wind safety after successful poll
        _logger.info("Weather data updated successfully");
    } else {
        _logger.warn("Failed to update weather data");
    }
    
    _forceUpdate = false;
}

bool WeatherPoller::shouldPollWeather() {
    if (!_pollingEnabled) {
        return false;
    }
    
    // Force update requested
    if (_forceUpdate) {
        return true;
    }
    
    unsigned long currentTime = millis();
    unsigned long timeSinceLastPoll = currentTime - _lastPollTime;
    unsigned long timeSinceLastSuccess = currentTime - _lastSuccessTime;
    
    // First boot scenario - poll immediately if we've never successfully updated
    if (_lastSuccessTime == 0 && timeSinceLastPoll > 5000) { // Wait 5 seconds after boot for system to settle
        _logger.debug("First weather poll attempt after boot");
        return true;
    }
    
    // Regular interval polling
    if (timeSinceLastPoll >= POLL_INTERVAL_MS) {
        return true;
    }
    
    // Retry on error after shorter interval
    if (timeSinceLastSuccess >= POLL_INTERVAL_MS && timeSinceLastPoll >= RETRY_INTERVAL_MS) {
        return true;
    }
    
    return false;
}

bool WeatherPoller::pollWeatherData() {
    String apiUrl = buildApiUrl();
    if (apiUrl.isEmpty()) {
        setErrorState("Failed to build API URL");
        return false;
    }
    
    _logger.debug("Polling weather API: " + apiUrl);
    
    // Use explicit scoping to ensure HTTPClient is properly destroyed
    bool success = false;
    {
        HTTPClient http;
        
        if (!http.begin(apiUrl)) {
            setErrorState("HTTP begin failed");
            return false;
        }
        
        // Configure HTTP client with explicit connection management
        http.setConnectTimeout(HTTP_TIMEOUT_MS);
        http.setTimeout(HTTP_TIMEOUT_MS);
        http.addHeader("User-Agent", "DiscoveryDish/1.0");
        http.addHeader("Connection", "close");  // Force connection close
        http.setReuse(false);  // Disable connection reuse to prevent stale connections
        
        int httpResponseCode = http.GET();
        
        if (httpResponseCode == 200) {
            String payload = http.getString();
            success = processWeatherResponse(payload);
            // Clear payload immediately after use
            payload = String();
        } else if (httpResponseCode == 401) {
            setErrorState("Invalid API key");
            _logger.error("WeatherAPI authentication failed - check API key");
        } else if (httpResponseCode == 403) {
            setErrorState("API key quota exceeded");
            _logger.error("WeatherAPI quota exceeded");
        } else if (httpResponseCode > 0) {
            setErrorState("HTTP error: " + String(httpResponseCode));
            _logger.error("WeatherAPI HTTP error: " + String(httpResponseCode));
        } else {
            setErrorState("Network error: " + String(httpResponseCode));
            _logger.error("WeatherAPI network error: " + String(httpResponseCode));
        }
        
        // Explicitly end the HTTP connection
        http.end();
        
    } // HTTPClient object destroyed here - forces cleanup
    
    // Force string pool cleanup if memory is low
    if (ESP.getFreeHeap() < 50000) {
        // Force string cleanup by creating and destroying temporary strings
        String temp = "";
        temp.reserve(0);
    }
    
    return success;
}

bool WeatherPoller::processWeatherResponse(const String& payload) {
    // Use explicit scoping for JSON document to ensure proper cleanup
    bool success = false;
    {
        // Use appropriate document size - increased slightly for safety
        DynamicJsonDocument doc(5120);  // 5KB should handle most responses
        
        DeserializationError error = deserializeJson(doc, payload);
        
        if (error) {
            String errorMsg = "JSON parse error: " + String(error.c_str());
            
            // Add more detailed error information for debugging
            if (error == DeserializationError::IncompleteInput) {
                errorMsg += " (Response appears truncated - received " + 
                           String(payload.length()) + " bytes)";
            } else if (error == DeserializationError::NoMemory) {
                errorMsg += " (Insufficient memory for JSON parsing - need ~" + 
                           String(payload.length() * 1.5) + " bytes)";
            }
            
            setErrorState(errorMsg);
            
            // Clear document before returning
            doc.clear();
            return false;
        }
        
        // Check for API errors
        if (doc.containsKey("error")) {
            String apiError = doc["error"]["message"].as<String>();
            setErrorState("API error: " + apiError);
            
            // Clear document before returning
            doc.clear();
            return false;
        }
        
        // Extract current and forecast weather data
        bool currentOk = extractCurrentWeather(doc);
        bool forecastOk = extractForecastWeather(doc);
        
        if (currentOk || forecastOk) {
            if (_weatherDataMutex != NULL && xSemaphoreTake(_weatherDataMutex, portMAX_DELAY) == pdTRUE) {
                _weatherData.dataValid = true;
                _weatherData.errorMessage = "";
                xSemaphoreGive(_weatherDataMutex);
            }
            success = true;
        } else {
            setErrorState("Failed to extract weather data");
            success = false;
        }
        
        // Explicitly clear the JSON document to free memory immediately
        doc.clear();
        
    } // DynamicJsonDocument destroyed here
    
    return success;
}

// =============================================================================
// WIND SAFETY METHODS
// =============================================================================

void WeatherPoller::updateWindSafetyStatus() {
    if (!_windSafetyEnabled) {
        setEmergencyStowState(false, "");
        return;
    }
    
    if (!isDataValid()) {
        _logger.warn("Cannot update wind safety - no valid weather data");
        return;
    }
    
    bool currentConditionsTriggered = checkCurrentWindConditions();
    bool forecastConditionsTriggered = checkForecastWindConditions();
    
    if (currentConditionsTriggered || forecastConditionsTriggered) {
        String reason = "";
        if (currentConditionsTriggered && forecastConditionsTriggered) {
            reason = "Current and forecast wind conditions exceed thresholds";
        } else if (currentConditionsTriggered) {
            reason = "Current wind conditions exceed thresholds";
        } else {
            reason = "Forecast wind conditions exceed thresholds";
        }
        
        setEmergencyStowState(true, reason);
    } else {
        // Check if we should clear the stow state (with hysteresis)
        if (_windSafetyMutex != NULL && xSemaphoreTake(_windSafetyMutex, portMAX_DELAY) == pdTRUE) {
            if (_windSafetyData.emergencyStowActive && 
                (millis() - _windSafetyData.stowActivatedTime) > STOW_HYSTERESIS_MS) {
                setEmergencyStowState(false, "");
            }
            xSemaphoreGive(_windSafetyMutex);
        }
    }
}

bool WeatherPoller::checkCurrentWindConditions() {
    WeatherData data = getWeatherData();
    
    float speedThreshold = _windSpeedThreshold.load();
    float gustThreshold = _windGustThreshold.load();
    
    bool speedExceeded = data.currentWindSpeed > speedThreshold;
    bool gustExceeded = data.currentWindGust > gustThreshold;
    
    if (speedExceeded || gustExceeded) {
        _logger.info("Current wind conditions exceed thresholds - Speed: " + 
                    String(data.currentWindSpeed, 1) + " km/h (limit: " + String(speedThreshold, 1) + 
                    "), Gust: " + String(data.currentWindGust, 1) + " km/h (limit: " + String(gustThreshold, 1) + ")");
        return true;
    }
    
    return false;
}

bool WeatherPoller::checkForecastWindConditions() {
    WeatherData data = getWeatherData();
    
    float speedThreshold = _windSpeedThreshold.load();
    float gustThreshold = _windGustThreshold.load();
    
    // Check next hour forecast (index 0)
    if (data.forecastWindSpeed[0] > speedThreshold || data.forecastWindGust[0] > gustThreshold) {
        _logger.info("Next hour forecast exceeds thresholds - Speed: " + 
                    String(data.forecastWindSpeed[0], 1) + " km/h (limit: " + String(speedThreshold, 1) + 
                    "), Gust: " + String(data.forecastWindGust[0], 1) + " km/h (limit: " + String(gustThreshold, 1) + ")");
        return true;
    }
    
    return false;
}

void WeatherPoller::setEmergencyStowState(bool active, const String& reason) {
    if (_windSafetyMutex != NULL && xSemaphoreTake(_windSafetyMutex, portMAX_DELAY) == pdTRUE) {
        bool wasActive = _windSafetyData.emergencyStowActive;
        
        _windSafetyData.emergencyStowActive = active;
        _windSafetyData.stowReason = reason;
        
        if (active) {
            _windSafetyData.stowActivatedTime = millis();
            
            // Calculate optimal stow direction based on current wind
            WeatherData data = getWeatherData();
            _windSafetyData.currentStowDirection = calculateOptimalStowDirection(data.currentWindDirection);
            
            if (!wasActive) {
                _logger.warn("EMERGENCY WIND STOW ACTIVATED: " + reason + 
                           " - Stow direction: " + String(_windSafetyData.currentStowDirection, 1) + "°");
            }
        } else {
            if (wasActive) {
                _logger.info("Emergency wind stow deactivated - conditions have improved");
            }
            _windSafetyData.currentStowDirection = 0.0;
        }
        
        xSemaphoreGive(_windSafetyMutex);
    }
}

float WeatherPoller::calculateOptimalStowDirection(float windDirection) {
    // Position dish edge-on to wind for minimum wind load
    // Choose the +90° or -90° option that requires less movement from current position
    
    float option1 = normalizeAngle(windDirection + 90.0);
    float option2 = normalizeAngle(windDirection - 90.0);
    
    // For now, default to +90° (can be improved to consider current dish position)
    return option1;
}

float WeatherPoller::getWindBasedHomePosition() {
    if (!_windBasedHomeEnabled || !isDataValid()) {
        return 0.0; // Default home position
    }
    
    WeatherData data = getWeatherData();
    return calculateOptimalStowDirection(data.currentWindDirection);
}

WindSafetyData WeatherPoller::getWindSafetyData() {
    WindSafetyData data;
    
    if (_windSafetyMutex != NULL && xSemaphoreTake(_windSafetyMutex, portMAX_DELAY) == pdTRUE) {
        data = _windSafetyData;
        xSemaphoreGive(_windSafetyMutex);
    }
    
    return data;
}

bool WeatherPoller::shouldActivateEmergencyStow() {
    WindSafetyData safetyData = getWindSafetyData();
    return safetyData.emergencyStowActive;
}

// =============================================================================
// WIND SAFETY CONFIGURATION METHODS
// =============================================================================

void WeatherPoller::setWindSafetyEnabled(bool enabled) {
    _windSafetyEnabled = enabled;
    _preferences.putBool("wind_safety_enabled", enabled);
    _logger.info("Wind safety " + String(enabled ? "enabled" : "disabled"));
    
    if (!enabled) {
        setEmergencyStowState(false, "");
    }
}

bool WeatherPoller::isWindSafetyEnabled() {
    return _windSafetyEnabled.load();
}

void WeatherPoller::setWindSpeedThreshold(float threshold) {
    if (threshold > 0 && threshold <= 200) {
        _windSpeedThreshold = threshold;
        _preferences.putFloat("wind_speed_threshold", threshold);
        _logger.info("Wind speed threshold set to: " + String(threshold, 1) + " km/h");
    }
}

float WeatherPoller::getWindSpeedThreshold() {
    return _windSpeedThreshold.load();
}

void WeatherPoller::setWindGustThreshold(float threshold) {
    if (threshold > 0 && threshold <= 200) {
        _windGustThreshold = threshold;
        _preferences.putFloat("wind_gust_threshold", threshold);
        _logger.info("Wind gust threshold set to: " + String(threshold, 1) + " km/h");
    }
}

float WeatherPoller::getWindGustThreshold() {
    return _windGustThreshold.load();
}

void WeatherPoller::setWindBasedHomeEnabled(bool enabled) {
    _windBasedHomeEnabled = enabled;
    _preferences.putBool("wind_based_home", enabled);
    _logger.info("Wind-based home positioning " + String(enabled ? "enabled" : "disabled"));
}

bool WeatherPoller::isWindBasedHomeEnabled() {
    return _windBasedHomeEnabled.load();
}

// =============================================================================
// DATA PROCESSING HELPERS
// =============================================================================

bool WeatherPoller::extractCurrentWeather(JsonDocument& doc) {
    if (!doc.containsKey("current")) {
        _logger.warn("No current weather data in WeatherAPI response");
        return false;
    }
    
    JsonObject current = doc["current"];
    
    if (_weatherDataMutex != NULL && xSemaphoreTake(_weatherDataMutex, portMAX_DELAY) == pdTRUE) {
        // Extract current weather values with validation
        _weatherData.currentWindSpeed = validateWindSpeed(current["wind_kph"]);
        _weatherData.currentWindDirection = validateWindDirection(current["wind_degree"]);
        _weatherData.currentWindGust = validateWindSpeed(current["gust_kph"]);
        
        // Handle time strings carefully to avoid memory leaks
        const char* timeStr = current["last_updated"];
        if (timeStr != nullptr) {
            _weatherData.currentTime = String(timeStr);
            _weatherData.lastUpdateTime = formatWeatherApiTime(_weatherData.currentTime);
        } else {
            _weatherData.currentTime = "Unknown";
            _weatherData.lastUpdateTime = "Unknown";
        }
        
        xSemaphoreGive(_weatherDataMutex);
        
        _logger.debug("Current wind: " + String(_weatherData.currentWindSpeed, 1) + " km/h, " +
                     "Direction: " + String(_weatherData.currentWindDirection, 0) + "°, " +
                     "Gusts: " + String(_weatherData.currentWindGust, 1) + " km/h");
        return true;
    }
    
    return false;
}

bool WeatherPoller::extractForecastWeather(JsonDocument& doc) {
    if (!doc.containsKey("forecast") || !doc["forecast"].containsKey("forecastday")) {
        _logger.warn("No forecast data in WeatherAPI response");
        return false;
    }
    
    JsonArray forecastDays = doc["forecast"]["forecastday"];
    if (forecastDays.size() == 0) {
        _logger.warn("Empty forecast array");
        return false;
    }
    
    JsonObject today = forecastDays[0];
    if (!today.containsKey("hour")) {
        _logger.warn("No hourly data in forecast");
        return false;
    }
    
    JsonArray hours = today["hour"];
    if (hours.size() == 0) {
        _logger.warn("Empty hourly forecast array");
        return false;
    }
    
    // Get current time from the API response to find our position
    String currentTimeStr = "";
    if (doc.containsKey("current") && doc["current"].containsKey("last_updated")) {
        const char* timeStr = doc["current"]["last_updated"];
        if (timeStr != nullptr) {
            currentTimeStr = String(timeStr);
        }
    }
    
    if (_weatherDataMutex != NULL && xSemaphoreTake(_weatherDataMutex, portMAX_DELAY) == pdTRUE) {
        int forecastCount = 0;
        int currentHour = getCurrentHourFromTime(currentTimeStr);
        
        _logger.debug("Current time: " + currentTimeStr + ", current hour: " + String(currentHour) + ", looking for hours > " + String(currentHour));
        
        // Find the next available hours starting from current hour + 1
        for (size_t i = 0; i < hours.size() && forecastCount < 3; i++) {
            JsonObject hour = hours[i];
            
            const char* hourTimeStr = hour["time"];
            if (hourTimeStr == nullptr) continue;
            
            String hourTime = String(hourTimeStr);
            int hourValue = getHourFromTimeString(hourTime);
            
            // Only include hours that are in the future (current hour + 1 or later)
            if (hourValue > currentHour) {
                _weatherData.forecastTimes[forecastCount] = hourTime;
                _weatherData.forecastWindSpeed[forecastCount] = validateWindSpeed(hour["wind_kph"]);
                _weatherData.forecastWindDirection[forecastCount] = validateWindDirection(hour["wind_degree"]);
                _weatherData.forecastWindGust[forecastCount] = validateWindSpeed(hour["gust_kph"]);
                
                _logger.debug("Forecast " + String(forecastCount) + ": " + hourTime + 
                             " - Wind: " + String(_weatherData.forecastWindSpeed[forecastCount], 1) + " km/h");
                
                forecastCount++;
            }
        }
        
        // If we couldn't find 3 future hours (maybe it's late in the day), 
        // try to get some from tomorrow if available
        if (forecastCount < 3 && forecastDays.size() > 1) {
            JsonObject tomorrow = forecastDays[1];
            if (tomorrow.containsKey("hour")) {
                JsonArray tomorrowHours = tomorrow["hour"];
                
                for (size_t i = 0; i < tomorrowHours.size() && forecastCount < 3; i++) {
                    JsonObject hour = tomorrowHours[i];
                    
                    const char* hourTimeStr = hour["time"];
                    if (hourTimeStr == nullptr) continue;
                    
                    String hourTime = String(hourTimeStr);
                    
                    _weatherData.forecastTimes[forecastCount] = hourTime;
                    _weatherData.forecastWindSpeed[forecastCount] = validateWindSpeed(hour["wind_kph"]);
                    _weatherData.forecastWindDirection[forecastCount] = validateWindDirection(hour["wind_degree"]);
                    _weatherData.forecastWindGust[forecastCount] = validateWindSpeed(hour["gust_kph"]);
                    
                    _logger.debug("Tomorrow forecast " + String(forecastCount) + ": " + hourTime + 
                                 " - Wind: " + String(_weatherData.forecastWindSpeed[forecastCount], 1) + " km/h");
                    
                    forecastCount++;
                }
            }
        }
        
        xSemaphoreGive(_weatherDataMutex);
        
        _logger.debug("Forecast extracted for next " + String(forecastCount) + " hours");
        return (forecastCount > 0);
    }
    
    return false;
}

void WeatherPoller::clearWeatherData() {
    if (_weatherDataMutex != NULL && xSemaphoreTake(_weatherDataMutex, portMAX_DELAY) == pdTRUE) {
        // Clear all data and explicitly reset strings to free memory
        _weatherData.currentWindSpeed = 0.0;
        _weatherData.currentWindGust = 0.0;
        _weatherData.currentWindDirection = 0.0;
        _weatherData.currentTime = "";
        _weatherData.lastUpdateTime = "Never";
        _weatherData.errorMessage = "";
        _weatherData.dataValid = false;
        
        // Clear forecast arrays
        for (int i = 0; i < 3; i++) {
            _weatherData.forecastWindSpeed[i] = 0.0;
            _weatherData.forecastWindGust[i] = 0.0;
            _weatherData.forecastWindDirection[i] = 0.0;
            _weatherData.forecastTimes[i] = "";
        }
        
        xSemaphoreGive(_weatherDataMutex);
    }
}

void WeatherPoller::setErrorState(const String& error) {
    if (_weatherDataMutex != NULL && xSemaphoreTake(_weatherDataMutex, portMAX_DELAY) == pdTRUE) {
        _weatherData.errorMessage = error;
        _weatherData.dataValid = false;
        xSemaphoreGive(_weatherDataMutex);
    }
    _logger.error("Weather polling error: " + error);
}

// =============================================================================
// CONFIGURATION METHODS
// =============================================================================

bool WeatherPoller::setLocation(float latitude, float longitude) {
    if (!isValidCoordinate(latitude, longitude)) {
        _logger.error("Invalid coordinates: " + String(latitude, 6) + ", " + String(longitude, 6));
        return false;
    }
    
    _latitude = latitude;
    _longitude = longitude;
    
    // Save to preferences
    _preferences.putFloat("weather_lat", latitude);
    _preferences.putFloat("weather_lon", longitude);
    
    _logger.info("Weather location set to: " + String(latitude, 6) + ", " + String(longitude, 6));
    
    // Clear old data and force update if now fully configured
    if (isFullyConfigured() && _pollingEnabled) {
        clearWeatherData();
        forceUpdate();
    }
    
    return true;
}

bool WeatherPoller::setApiKey(const String& apiKey) {
    String trimmedKey = apiKey;
    trimmedKey.trim();
    
    if (!isValidApiKey(trimmedKey)) {
        _logger.error("Invalid API key format");
        return false;
    }
    
    if (_apiKeyMutex != NULL && xSemaphoreTake(_apiKeyMutex, portMAX_DELAY) == pdTRUE) {
        _apiKey = trimmedKey;
        xSemaphoreGive(_apiKeyMutex);
    }
    
    // Save to preferences
    _preferences.putString("weather_api_key", trimmedKey);
    
    _logger.info("WeatherAPI key configured");
    
    // Clear old data and force update if now fully configured
    if (isFullyConfigured() && _pollingEnabled) {
        clearWeatherData();
        forceUpdate();
    }
    
    return true;
}

float WeatherPoller::getLatitude() {
    return _latitude.load();
}

float WeatherPoller::getLongitude() {
    return _longitude.load();
}

String WeatherPoller::getApiKey() {
    String key = "";
    if (_apiKeyMutex != NULL && xSemaphoreTake(_apiKeyMutex, portMAX_DELAY) == pdTRUE) {
        key = _apiKey;
        xSemaphoreGive(_apiKeyMutex);
    }
    return key;
}

bool WeatherPoller::isLocationConfigured() {
    float lat = _latitude.load();
    float lon = _longitude.load();
    return isValidCoordinate(lat, lon);
}

bool WeatherPoller::isApiKeyConfigured() {
    String key = getApiKey();
    return isValidApiKey(key);
}

bool WeatherPoller::isFullyConfigured() {
    return isLocationConfigured() && isApiKeyConfigured();
}

// =============================================================================
// DATA ACCESS METHODS
// =============================================================================

WeatherData WeatherPoller::getWeatherData() {
    WeatherData data;
    
    if (_weatherDataMutex != NULL && xSemaphoreTake(_weatherDataMutex, portMAX_DELAY) == pdTRUE) {
        data = _weatherData;
        xSemaphoreGive(_weatherDataMutex);
    }
    
    return data;
}

bool WeatherPoller::isDataValid() {
    bool valid = false;
    
    if (_weatherDataMutex != NULL && xSemaphoreTake(_weatherDataMutex, portMAX_DELAY) == pdTRUE) {
        valid = _weatherData.dataValid;
        xSemaphoreGive(_weatherDataMutex);
    }
    
    return valid;
}

String WeatherPoller::getLastError() {
    String error = "";
    
    if (_weatherDataMutex != NULL && xSemaphoreTake(_weatherDataMutex, portMAX_DELAY) == pdTRUE) {
        error = _weatherData.errorMessage;
        xSemaphoreGive(_weatherDataMutex);
    }
    
    return error;
}

unsigned long WeatherPoller::getLastUpdateTime() {
    return _lastSuccessTime.load();
}

// =============================================================================
// CONTROL METHODS
// =============================================================================

void WeatherPoller::forceUpdate() {
    _forceUpdate = true;
    _logger.debug("Weather update forced");
}

bool WeatherPoller::isPollingEnabled() {
    return _pollingEnabled.load();
}

void WeatherPoller::setPollingEnabled(bool enabled) {
    _pollingEnabled = enabled;
    _preferences.putBool("weather_enabled", enabled);
    _logger.info("Weather polling " + String(enabled ? "enabled" : "disabled"));
    
    if (!enabled) {
        clearWeatherData();
        setEmergencyStowState(false, "");
    }
}

// =============================================================================
// UTILITY METHODS
// =============================================================================

String WeatherPoller::buildApiUrl() {
    if (!isFullyConfigured()) {
        return "";
    }
    
    String apiKey = getApiKey();
    if (apiKey.isEmpty()) {
        return "";
    }
    
    String url = "http://api.weatherapi.com/v1/forecast.json";
    url += "?key=" + apiKey;
    url += "&q=" + String(_latitude.load(), 6) + "," + String(_longitude.load(), 6);
    url += "&days=2";
    url += "&aqi=no";
    url += "&alerts=no";
    
    return url;
}

float WeatherPoller::validateWindSpeed(float speed) {
    if (isnan(speed) || speed < 0) {
        return 0.0;
    }
    // Cap at reasonable maximum (500 km/h)
    return min(speed, 500.0f);
}

float WeatherPoller::validateWindDirection(float direction) {
    if (isnan(direction)) {
        return 0.0;
    }
    // Normalize to 0-360 range
    while (direction < 0) direction += 360;
    while (direction >= 360) direction -= 360;
    return direction;
}

float WeatherPoller::normalizeAngle(float angle) {
    while (angle < 0) angle += 360;
    while (angle >= 360) angle -= 360;
    return angle;
}

int WeatherPoller::getCurrentHourFromTime(const String& timeStr) {
    // Parse time string like "2024-01-15 17:30" to extract hour (17)
    if (timeStr.length() == 0) {
        return -1; // Invalid time
    }
    
    int spaceIndex = timeStr.indexOf(' ');
    if (spaceIndex == -1) {
        return -1; // No space found
    }
    
    String timePart = timeStr.substring(spaceIndex + 1); // "17:30"
    int colonIndex = timePart.indexOf(':');
    if (colonIndex == -1) {
        return -1; // No colon found
    }
    
    String hourStr = timePart.substring(0, colonIndex); // "17"
    return hourStr.toInt();
}

int WeatherPoller::getHourFromTimeString(const String& timeStr) {
    // Parse time string like "2024-01-15 18:00" to extract hour (18)
    return getCurrentHourFromTime(timeStr);
}

String WeatherPoller::formatWeatherApiTime(const String& apiTime) {
    // WeatherAPI returns time like "2024-01-15 14:30" (local time at location)
    // We'll just clean it up for display
    if (apiTime.length() == 0) {
        return "Unknown";
    }
    
    // Find the space between date and time
    int spaceIndex = apiTime.indexOf(' ');
    if (spaceIndex == -1) {
        return apiTime; // Return as-is if format is unexpected
    }
    
    String datePart = apiTime.substring(0, spaceIndex);
    String timePart = apiTime.substring(spaceIndex + 1);
    
    // Return in format "Jan 15, 14:30" for better readability
    // For now, just return time part since that's most relevant
    return timePart + " (local)";
}

String WeatherPoller::getRelativeUpdateTime() {
    // This method is kept for backward compatibility but uses weather data timestamp
    WeatherData data = getWeatherData();
    return data.lastUpdateTime;
}

bool WeatherPoller::isValidCoordinate(float lat, float lon) {
    return (lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0 && 
            (lat != 0.0 || lon != 0.0)); // Exclude 0,0 as it's likely unset
}

bool WeatherPoller::isValidApiKey(const String& key) {
    // WeatherAPI.com keys are typically 32 characters long and alphanumeric
    if (key.length() < 16 || key.length() > 64) {
        return false;
    }
    
    // Check for basic alphanumeric pattern (letters, numbers, possibly some symbols)
    for (int i = 0; i < key.length(); i++) {
        char c = key.charAt(i);
        if (!isAlphaNumeric(c) && c != '_' && c != '-') {
            return false;
        }
    }
    
    return true;
}