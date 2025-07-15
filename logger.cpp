/*
 * Firmware for the discovery-drive satellite dish rotator.
 * Logger - Enable logging levels and logging to the web UI and over serial.
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

#include "logger.h"

// =============================================================================
// CONSTRUCTOR AND INITIALIZATION
// =============================================================================

Logger::Logger(Preferences& prefs) : _preferences(prefs) {
}

void Logger::begin() {
    // Create mutex for thread-safe log access
    _logMessagesMutex = xSemaphoreCreateMutex();
    
    // Load saved debug level from preferences
    int savedDebugLevel = _preferences.getInt("debugLevel", 1);
    _currentDebugLevel = savedDebugLevel;
    
    // Load saved serial output setting from preferences (set directly, don't use setter during init)
    bool savedSerialOutputDisabled = _preferences.getBool("serialDisabled", false);
    _serialOutputDisabled = savedSerialOutputDisabled;
    
    info("Logger initialized with debug level: " + String(savedDebugLevel) + 
         ", serial output " + String(savedSerialOutputDisabled ? "disabled" : "enabled"));
}

// =============================================================================
// CORE FUNCTIONALITY
// =============================================================================

void Logger::logMessage(LogLevel level, const String& message) {
    // Check if message should be logged based on current debug level
    if (level > _currentDebugLevel) {
        return;
    }
    
    String levelStr = getLevelString(level);
    String fullMessage = levelStr + message;
    
    // Output to Serial console only if not disabled
    if (!_serialOutputDisabled) {
        Serial.println(fullMessage);
    }
    
    // Add to web log buffer for web interface
    addToWebLog(fullMessage);
}

// =============================================================================
// DEBUG LEVEL MANAGEMENT
// =============================================================================

void Logger::setDebugLevel(int level) {
    if (level >= LOG_NONE && level <= LOG_VERBOSE) {
        _currentDebugLevel = level;
        _preferences.putInt("debugLevel", level);
        info("Debug level changed to: " + String(level));
    }
}

int Logger::getDebugLevel() {
    return _currentDebugLevel;
}

// =============================================================================
// SERIAL OUTPUT CONTROL
// =============================================================================

void Logger::setSerialOutputDisabled(bool disabled) {
    _serialOutputDisabled = disabled;
    _preferences.putBool("serialDisabled", disabled);
    info("Serial output " + String(disabled ? "disabled" : "enabled"));
}

bool Logger::getSerialOutputDisabled() {
    return _serialOutputDisabled;
}

// =============================================================================
// WEB INTERFACE METHODS
// =============================================================================

String Logger::getNewLogMessages() {
    String result = "";
    
    if (_logMessagesMutex != NULL && xSemaphoreTake(_logMessagesMutex, portMAX_DELAY) == pdTRUE) {
        result = _newMessages;
        _newMessages = "";  // Clear buffer after reading
        xSemaphoreGive(_logMessagesMutex);
    }
    
    return result;
}

void Logger::addToWebLog(const String& message) {
    if (_logMessagesMutex == NULL) {
        return;
    }
    
    if (xSemaphoreTake(_logMessagesMutex, portMAX_DELAY) == pdTRUE) {
        // Add timestamp to message
        String timestampedMessage = "[" + String(millis()) + "] " + message;
        
        // Append to new messages buffer
        if (_newMessages.length() > 0) {
            _newMessages += "\n";
        }
        _newMessages += timestampedMessage;
        
        // Maintain reasonable buffer size
        manageBufferSize();
        
        xSemaphoreGive(_logMessagesMutex);
    }
}

void Logger::manageBufferSize() {
    const size_t MAX_BUFFER_SIZE = 10000;  // 10KB buffer limit
    
    if (_newMessages.length() > MAX_BUFFER_SIZE) {
        // Keep the most recent messages by removing older entries
        int splitPos = _newMessages.indexOf('\n', _newMessages.length() / 2);
        if (splitPos > 0) {
            _newMessages = _newMessages.substring(splitPos + 1);
        }
    }
}

// =============================================================================
// UTILITY METHODS
// =============================================================================

String Logger::getLevelString(LogLevel level) {
    switch (level) {
        case LOG_ERROR:   return "[ERROR] ";
        case LOG_WARN:    return "[WARN]  ";
        case LOG_INFO:    return "[INFO]  ";
        case LOG_DEBUG:   return "[DEBUG] ";
        case LOG_VERBOSE: return "[VERB]  ";
        default:          return "[LOG]   ";
    }
}