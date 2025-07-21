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
    
    // Load saved configuration
    _latitude = _preferences.getFloat("weather_lat", 0.0);
    _longitude = _preferences.getFloat("weather_lon", 0.0);
    _pollingEnabled = _preferences.getBool("weather_enabled", true);
    _apiKey = _preferences.getString("weather_api_key", "");
    
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
    HTTPClient http;
    
    String apiUrl = buildApiUrl();
    if (apiUrl.isEmpty()) {
        setErrorState("Failed to build API URL");
        return false;
    }
    
    _logger.debug("Polling weather API: " + apiUrl);
    
    if (!http.begin(apiUrl)) {
        setErrorState("HTTP begin failed");
        return false;
    }
    
    // Configure HTTP client
    http.setConnectTimeout(HTTP_TIMEOUT_MS);
    http.setTimeout(HTTP_TIMEOUT_MS);
    http.addHeader("User-Agent", "DiscoveryDish/1.0");
    
    int httpResponseCode = http.GET();
    bool success = false;
    
    if (httpResponseCode == 200) {
        String payload = http.getString();
        success = processWeatherResponse(payload);
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
    
    http.end();
    return success;
}

bool WeatherPoller::processWeatherResponse(const String& payload) {
    DynamicJsonDocument doc(4096);
    DeserializationError error = deserializeJson(doc, payload);
    
    if (error) {
        setErrorState("JSON parse error: " + String(error.c_str()));
        return false;
    }
    
    // Check for API errors
    if (doc.containsKey("error")) {
        String apiError = doc["reason"].as<String>();
        setErrorState("API error: " + apiError);
        return false;
    }
    
    // Extract current and forecast weather data
    bool currentOk = extractCurrentWeather(doc);
    bool forecastOk = extractForecastWeather(doc);
    
    if (currentOk || forecastOk) {
        if (_weatherDataMutex != NULL && xSemaphoreTake(_weatherDataMutex, portMAX_DELAY) == pdTRUE) {
            _weatherData.dataValid = true;
            _weatherData.errorMessage = "";
            // lastUpdateTime is now set directly from WeatherAPI's last_updated field in extractCurrentWeather()
            xSemaphoreGive(_weatherDataMutex);
        }
        return true;
    } else {
        setErrorState("Failed to extract weather data");
        return false;
    }
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
        _weatherData.currentTime = current["last_updated"].as<String>();
        
        // Use WeatherAPI's last_updated timestamp for our "Last Update" field too
        _weatherData.lastUpdateTime = formatWeatherApiTime(current["last_updated"].as<String>());
        
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
        currentTimeStr = doc["current"]["last_updated"].as<String>();
    }
    
    if (_weatherDataMutex != NULL && xSemaphoreTake(_weatherDataMutex, portMAX_DELAY) == pdTRUE) {
        int forecastCount = 0;
        int currentHour = getCurrentHourFromTime(currentTimeStr);
        
        _logger.debug("Current time: " + currentTimeStr + ", current hour: " + String(currentHour) + ", looking for hours > " + String(currentHour));
        
        // Find the next available hours starting from current hour + 1
        for (int i = 0; i < hours.size() && forecastCount < 3; i++) {
            JsonObject hour = hours[i];
            String hourTime = hour["time"].as<String>();
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
                
                for (int i = 0; i < tomorrowHours.size() && forecastCount < 3; i++) {
                    JsonObject hour = tomorrowHours[i];
                    String hourTime = hour["time"].as<String>();
                    
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
        _weatherData = WeatherData(); // Reset to default values
        _weatherData.lastUpdateTime = "Never"; // Set appropriate default
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
    url += "&days=1";
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