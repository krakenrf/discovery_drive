/*
 * Firmware for the discovery-drive satellite dish rotator.
 * INA219 Manager - Manage the INA219 current & voltage sensor.
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
 
#ifndef INA219_MANAGER_H
#define INA219_MANAGER_H

// System includes
#include <Adafruit_INA219.h>

// Custom includes
#include "logger.h"

class INA219Manager {
public:
    // Constructor
    INA219Manager(Logger& logger);

    // Core functionality
    void begin();
    void ReadData();

    // Data access methods
    float getCurrent();
    float getLoadVoltage();
    float getPower();

private:
    // Dependencies
    Logger& _logger;

    // Hardware configuration
    static constexpr int _INA219_I2C_ADDRESS = 0x45;
    static constexpr int _AVERAGING_ARRAY_SIZE = 10;

    // Thread synchronization
    SemaphoreHandle_t powerMutex = NULL;

    // Sensor data (thread-safe)
    float _current_mA = 0;
    float _loadvoltage = 0;
    float _power = 0;

    // Voltage averaging
    float _voltageReadings[_AVERAGING_ARRAY_SIZE];
    int _voltageIndex = 0;
    int _voltageCount = 0;
    float _averagedVoltage = 0;

    // Current averaging
    float _currentReadings[_AVERAGING_ARRAY_SIZE];
    int _currentIndex = 0;
    int _currentCount = 0;
    float _averagedCurrent = 0;

    // Hardware interface
    Adafruit_INA219 _ina219;

    // Private helper methods
    void updateVoltageAverage(float newVoltage);
    float calculateVoltageAverage();
    void updateCurrentAverage(float newCurrent);
    float calculateCurrentAverage();
};

#endif // INA219_MANAGER_H