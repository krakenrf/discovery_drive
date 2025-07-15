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

#ifndef LOGGER_H
#define LOGGER_H

// System includes
#include <Arduino.h>
#include <Preferences.h>
#include <atomic>

// Log level enumeration
enum LogLevel {
    LOG_NONE = 0,
    LOG_ERROR = 1,
    LOG_WARN = 2,
    LOG_INFO = 3,
    LOG_DEBUG = 4,
    LOG_VERBOSE = 5
};

class Logger {
public:
    // Constructor
    Logger(Preferences& prefs);

    // Core functionality
    void begin();
    void logMessage(LogLevel level, const String& message);

    // Debug level management
    void setDebugLevel(int level);
    int getDebugLevel();

    // Serial output control
    void setSerialOutputDisabled(bool disabled);
    bool getSerialOutputDisabled();

    // Web interface methods
    String getNewLogMessages();  // Get new messages since last call

    // Convenience logging methods
    void error(const String& message) { logMessage(LOG_ERROR, message); }
    void warn(const String& message) { logMessage(LOG_WARN, message); }
    void info(const String& message) { logMessage(LOG_INFO, message); }
    void debug(const String& message) { logMessage(LOG_DEBUG, message); }
    void verbose(const String& message) { logMessage(LOG_VERBOSE, message); }

private:
    // Dependencies
    Preferences& _preferences;

    // Configuration
    std::atomic<int> _currentDebugLevel = 1;  // Default to ERROR level
    std::atomic<bool> _serialOutputDisabled = false;  // Default to enabled

    // Thread synchronization
    SemaphoreHandle_t _logMessagesMutex = NULL;

    // Web log state
    String _newMessages = "";           // Store new messages since last request
    bool _clearRequested = false;       // Flag to signal browser to clear

    // Utility methods
    void addToWebLog(const String& message);
    void manageBufferSize();
    String getLevelString(LogLevel level);
};

#endif // LOGGER_H