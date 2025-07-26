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
 
#include "ina219_manager.h"

// =============================================================================
// CONSTRUCTOR AND INITIALIZATION
// =============================================================================

INA219Manager::INA219Manager(Logger& logger)
    : _logger(logger), _ina219(_INA219_I2C_ADDRESS) {
    
    // Initialize voltage averaging array
    for (int i = 0; i < _AVERAGING_ARRAY_SIZE; i++) {
        _voltageReadings[i] = 0.0;
    }
    _voltageIndex = 0;
    _voltageCount = 0;
    _averagedVoltage = 0.0;

    // Initialize current averaging array
    for (int i = 0; i < _AVERAGING_ARRAY_SIZE; i++) {
        _currentReadings[i] = 0.0;
    }
    _currentIndex = 0;
    _currentCount = 0;
    _averagedCurrent = 0.0;
}

void INA219Manager::begin() {
    // Create mutex for thread-safe data access
    powerMutex = xSemaphoreCreateMutex();

    // Initialize INA219 sensor with error handling
    while (!_ina219.begin()) {
        _logger.error("Failed to find INA219 chip");
        delay(1000); // Wait before retrying
    }

    _logger.info("INA219 sensor initialized successfully");
    
    // Perform initial data reading
    ReadData();
}

// =============================================================================
// CORE FUNCTIONALITY
// =============================================================================

void INA219Manager::ReadData() {
    if (xSemaphoreTake(powerMutex, portMAX_DELAY) == pdTRUE) {
        // Read raw sensor values atomically
        float shuntvoltage = _ina219.getShuntVoltage_mV();
        float busvoltage = _ina219.getBusVoltage_V();
        
        // Calculate raw values
        float rawLoadVoltage = busvoltage + (shuntvoltage / 1000);
        float rawCurrent = shuntvoltage / 0.01; // Convert to mA
        
        // Update averaging for both voltage and current
        updateVoltageAverage(rawLoadVoltage);
        updateCurrentAverage(rawCurrent);
        
        // Store averaged values
        _current_mA = _averagedCurrent;         // Use averaged current
        _loadvoltage = _averagedVoltage;        // Use averaged voltage
        _power = _loadvoltage * (_current_mA / 1000);  // Power in W using both averaged values
        
        xSemaphoreGive(powerMutex);
    }
}

// =============================================================================
// VOLTAGE AVERAGING METHODS
// =============================================================================

void INA219Manager::updateVoltageAverage(float newVoltage) {
    // Store the new voltage reading in the circular buffer
    _voltageReadings[_voltageIndex] = newVoltage;
    
    // Move to next position in circular buffer
    _voltageIndex = (_voltageIndex + 1) % _AVERAGING_ARRAY_SIZE;
    
    // Track how many readings we have (up to array size)
    if (_voltageCount < _AVERAGING_ARRAY_SIZE) {
        _voltageCount++;
    }
    
    // Calculate the new average
    _averagedVoltage = calculateVoltageAverage();
}

float INA219Manager::calculateVoltageAverage() {
    if (_voltageCount == 0) {
        return 0.0;
    }
    
    float sum = 0.0;
    
    // Sum only the number of readings we actually have
    for (int i = 0; i < _voltageCount; i++) {
        sum += _voltageReadings[i];
    }
    
    return sum / _voltageCount;
}

// =============================================================================
// CURRENT AVERAGING METHODS
// =============================================================================

void INA219Manager::updateCurrentAverage(float newCurrent) {
    // Store the new current reading in the circular buffer
    _currentReadings[_currentIndex] = newCurrent;
    
    // Move to next position in circular buffer
    _currentIndex = (_currentIndex + 1) % _AVERAGING_ARRAY_SIZE;
    
    // Track how many readings we have (up to array size)
    if (_currentCount < _AVERAGING_ARRAY_SIZE) {
        _currentCount++;
    }
    
    // Calculate the new average
    _averagedCurrent = calculateCurrentAverage();
}

float INA219Manager::calculateCurrentAverage() {
    if (_currentCount == 0) {
        return 0.0;
    }
    
    float sum = 0.0;
    
    // Sum only the number of readings we actually have
    for (int i = 0; i < _currentCount; i++) {
        sum += _currentReadings[i];
    }
    
    return sum / _currentCount;
}

// =============================================================================
// DATA ACCESS METHODS
// =============================================================================

float INA219Manager::getCurrent() {
    float result = 0;
    if (xSemaphoreTake(powerMutex, portMAX_DELAY) == pdTRUE) {
        result = _current_mA;  // This is now the averaged current
        xSemaphoreGive(powerMutex);
    }
    return result;
}

float INA219Manager::getLoadVoltage() {
    float result = 0;
    if (xSemaphoreTake(powerMutex, portMAX_DELAY) == pdTRUE) {
        result = _loadvoltage;  // This is the averaged voltage
        xSemaphoreGive(powerMutex);
    }
    return result;
}

float INA219Manager::getPower() {
    float result = 0;
    if (xSemaphoreTake(powerMutex, portMAX_DELAY) == pdTRUE) {
        result = _power;  // This is calculated using both averaged voltage and current
        xSemaphoreGive(powerMutex);
    }
    return result;
}