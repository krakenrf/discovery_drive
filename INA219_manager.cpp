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
        
        // Calculate derived values
        _current_mA = shuntvoltage / 0.01;  // Convert to mA
        _loadvoltage = busvoltage + (shuntvoltage / 1000);  // Load voltage in V
        _power = _loadvoltage * (_current_mA / 1000);  // Power in W
        
        xSemaphoreGive(powerMutex);
    }
}

// =============================================================================
// DATA ACCESS METHODS
// =============================================================================

float INA219Manager::getCurrent() {
    float result = 0;
    if (xSemaphoreTake(powerMutex, portMAX_DELAY) == pdTRUE) {
        result = _current_mA;
        xSemaphoreGive(powerMutex);
    }
    return result;
}

float INA219Manager::getLoadVoltage() {
    float result = 0;
    if (xSemaphoreTake(powerMutex, portMAX_DELAY) == pdTRUE) {
        result = _loadvoltage;
        xSemaphoreGive(powerMutex);
    }
    return result;
}

float INA219Manager::getPower() {
    float result = 0;
    if (xSemaphoreTake(powerMutex, portMAX_DELAY) == pdTRUE) {
        result = _power;
        xSemaphoreGive(powerMutex);
    }
    return result;
}