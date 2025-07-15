/*
 * Firmware for the discovery-drive satellite dish rotator.
 * Serial Manager - Manage serial rotctl connections.
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

#ifndef SERIAL_MANAGER_H
#define SERIAL_MANAGER_H

// System includes
#include <Arduino.h>
#include <Preferences.h>

// Custom includes
#include "motor_controller.h"
#include "logger.h"

class SerialManager {
public:
    // Constructor
    SerialManager(Preferences& prefs, MotorSensorController& motorController, Logger& logger);

    // Core functionality
    void begin();
    void runSerialLoop();

    // Public state variables
    std::atomic<bool> serialActive = false;

private:
    // Dependencies
    Preferences& _preferences;
    MotorSensorController& _motorSensorCtrl;
    Logger& _logger;

    // Core functionality helpers
    void readSerialInput();
    void processCommand();
    void updateSerialActivityStatus();
    void resetInputBuffer();

    // Command processing methods
    bool processPositionQueries();
    bool processPositionCommands();
    bool processSetPositionCommands();
    bool processCalibrationCommands();
    bool processSystemCommands();

    // Utility methods
    void parseAndSetPosition();
    float validateAndCleanAzimuth(float az);
    float validateAndCleanElevation(float el);
    void printStatusInfo();
    void updateSerialActivity();

    // Configuration constants
    static constexpr unsigned long _serialActiveTimeout = 10000;  // 10 seconds timeout

    // Serial communication state
    String _inputString = "";
    bool _stringComplete = false;
    unsigned long _lastSerialActivity = 0;  // Timestamp of last serial activity
};

#endif // SERIAL_MANAGER_H