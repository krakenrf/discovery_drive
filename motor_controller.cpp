/*
 * Firmware for the discovery-drive satellite dish rotator.
 * Motor Controller - Manage the movement and safety of the motors.
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

#include "motor_controller.h"
#include "weather_poller.h"  // Include for wind safety integration

// =============================================================================
// CONSTRUCTOR AND INITIALIZATION
// =============================================================================

MotorSensorController::MotorSensorController(Preferences& prefs, INA219Manager& ina219Manager, Logger& logger) 
    : _preferences(prefs), ina219Manager(ina219Manager), _logger(logger) {
    
    // Create mutexes for thread-safe access
    _setPointMutex = xSemaphoreCreateMutex();
    _getAngleMutex = xSemaphoreCreateMutex();
    _correctedAngleMutex = xSemaphoreCreateMutex();
    _errorMutex = xSemaphoreCreateMutex();
    _el_startAngleMutex = xSemaphoreCreateMutex();
    _windStowMutex = xSemaphoreCreateMutex();
    
    // Initialize wind tracking
    _lastManualSetpointTime = millis();
}

void MotorSensorController::begin() {
    // Load configuration parameters from preferences
    P_el = _preferences.getInt("P_el", 100);
    P_az = _preferences.getInt("P_az", 5);
    MIN_EL_SPEED = _preferences.getInt("MIN_EL_SPEED", 50);
    MIN_AZ_SPEED = _preferences.getInt("MIN_AZ_SPEED", 100);
    _MIN_AZ_TOLERANCE = _preferences.getFloat("MIN_AZ_TOL", 1.5);
    _MIN_EL_TOLERANCE = _preferences.getFloat("MIN_EL_TOL", 0.1);
    _maxPowerBeforeFault = _preferences.getInt("MAX_POWER", 10);
    _minVoltageThreshold = _preferences.getInt("MIN_VOLTAGE", 6);

    // Configure motor control pins
    pinMode(_pwm_pin_az, OUTPUT);
    digitalWrite(_pwm_pin_az, 1);
    pinMode(_pwm_pin_el, OUTPUT);
    digitalWrite(_pwm_pin_el, 1);
    pinMode(_ccw_pin_az, OUTPUT);
    digitalWrite(_ccw_pin_az, 0);
    pinMode(_ccw_pin_el, OUTPUT);
    digitalWrite(_ccw_pin_el, 0);

    // Load motor speed settings
    max_dual_motor_az_speed = _preferences.getInt("maxDMAzSpeed", MAX_AZ_SPEED);
    max_dual_motor_el_speed = _preferences.getInt("maxDMElSpeed", MAX_EL_SPEED);
    max_single_motor_az_speed = _preferences.getInt("maxSMAzSpeed", 0);
    max_single_motor_el_speed = _preferences.getInt("maxSMElSpeed", 0);
    singleMotorMode = _preferences.getBool("singleMotorMode", false);

    // Check magnet presence for both sensors
    int az_magnetStatus = checkMagnetPresence(_az_hall_i2c_addr);
    delay(500);
    int el_magnetStatus = checkMagnetPresence(_el_hall_i2c_addr);

    // Log magnet status
    _logger.info("AZ Magnet Detected (MD): " + String((az_magnetStatus & 32) > 0));
    _logger.info("AZ Magnet Too Weak (ML): " + String((az_magnetStatus & 16) > 0));
    _logger.info("AZ Magnet Too Strong (MH): " + String((az_magnetStatus & 8) > 0));
    _logger.info("EL Magnet Detected (MD): " + String((el_magnetStatus & 32) > 0));
    _logger.info("EL Magnet Too Weak (ML): " + String((el_magnetStatus & 16) > 0));
    _logger.info("EL Magnet Too Strong (MH): " + String((el_magnetStatus & 8) > 0));

    // Set magnet fault flags
    if ((az_magnetStatus & 32) == 0) {
        _logger.error("NO AZ MAGNET DETECTED!");
        magnetFault = true;
    }
    
    if ((el_magnetStatus & 32) == 0) {
        _logger.error("NO EL MAGNET DETECTED!");
        magnetFault = true;
    }

    // Initialize azimuth positioning
    float degAngleAz = getAvgAngle(_az_hall_i2c_addr);
    _az_startAngle = 10; // Avoid 0 to prevent backlash switching between 0 and 359
    setCorrectedAngleAz(correctAngle(_az_startAngle, degAngleAz));
    needs_unwind = _preferences.getInt("needs_unwind", 0);

    // Initialize elevation positioning
    float degAngleEl = getAvgAngle(_el_hall_i2c_addr);
    setElStartAngle(_preferences.getFloat("el_cal", degAngleEl));
    _logger.info("EL START ANGLE: " + String(getElStartAngle()));
    setCorrectedAngleEl(correctAngle(getElStartAngle(), degAngleEl));

    // Set home position
    setSetPointAzInternal(0);
    setSetPointElInternal(0);
    
    // Initialize manual setpoint time
    _lastManualSetpointTime = millis();
}

// =============================================================================
// WEATHER INTEGRATION
// =============================================================================

void MotorSensorController::setWeatherPoller(WeatherPoller* weatherPoller) {
    _weatherPoller = weatherPoller;
    _logger.info("Weather poller integration enabled");
}

// =============================================================================
// MAIN CONTROL LOOPS
// =============================================================================

void MotorSensorController::runControlLoop() {
    // Update wind stow status first
    updateWindStowStatus();
    
    // Update wind tracking status
    updateWindTrackingStatus();
    
    // Get current setpoints and update flags
    float current_setpoint_az = getSetPointAz();
    float current_setpoint_el = getSetPointEl();    
    bool setPointAzUpdated = _setPointAzUpdated;
    bool setPointElUpdated = _setPointElUpdated;

    // Reset update flags
    if (_setPointAzUpdated) _setPointAzUpdated = false;
    if (_setPointElUpdated) _setPointElUpdated = false;

    // Read and process azimuth angle
    float degAngleAz = getAvgAngle(_az_hall_i2c_addr);
    setCorrectedAngleAz(correctAngle(_az_startAngle, degAngleAz));
    
    if (!calMode) {
        calcIfNeedsUnwind(getCorrectedAngleAz());
    }

    // Read and process elevation angle
    float degAngleEl = getAvgAngle(_el_hall_i2c_addr);
    setCorrectedAngleEl(correctAngle(getElStartAngle(), degAngleEl));

    // Calculate control errors
    angle_shortest_error_az(current_setpoint_az, getCorrectedAngleAz());
    angle_error_el(current_setpoint_el, getCorrectedAngleEl());

    // Update error tracking for convergence safety
    if (!calMode) {
        updateErrorTracking();
        //checkStall(); // TEMP DISABLE
    }

    // Execute control logic - but check for movement blocking first
    if (!calMode) {
        updateMotorControl(current_setpoint_az, current_setpoint_el, setPointAzUpdated, setPointElUpdated);
        updateMotorPriority(setPointAzUpdated, setPointElUpdated);
        
        // Apply wind stow movement blocking
        if (shouldBlockMovement()) {
            // Override motor states to stop movement
            setPointState_az = false;
            setPointState_el = false;
        }
        
        actuate_motor_az(MIN_AZ_SPEED);
        actuate_motor_el(MIN_EL_SPEED);
    } else {
        handleCalibrationMode();
    }

    handleOscillationDetection();
}

void MotorSensorController::runSafetyLoop() {
    String errorText = "";
    bool hasNewErrors = false;

    // Check fault conditions
    if (badAngleFlag) {
        global_fault = true;
        errorText += "Bad angle.\n";
        hasNewErrors = true;
    }

    if (magnetFault) {
        global_fault = true;
        errorText += "MAGNET NOT DETECTED.\n";
        hasNewErrors = true;
    }

    if (i2cErrorFlag_az) {
        global_fault = true;
        errorText += "Communications error in AZ i2c communications.\n";
        hasNewErrors = true;
    }

    if (i2cErrorFlag_el) {
        global_fault = true;
        errorText += "Communications error in EL i2c communications.\n";
        hasNewErrors = true;
    }

    // Check elevation bounds (if not in calibration mode)
    if (!calMode) {
        if ((getCorrectedAngleEl() > 95 && getCorrectedAngleEl() < 355) || isnan(getCorrectedAngleEl())) {
            outOfBoundsFault = true;
        }

        if (outOfBoundsFault) {
            global_fault = true;
            errorText += "EL went out of bounds. Value: " + String(getCorrectedAngleEl()) + "\n";
            hasNewErrors = true;
        }
    }

    // Check azimuth over-spin
    if (!calMode) {
        if (abs(needs_unwind) > 1) overSpinFault = true;

        if (overSpinFault) {
            global_fault = true;
            errorText += "Needs_unwind went beyond 1, AZ has over spun. Needs_unwind value: " + String(needs_unwind) + "\n";
            hasNewErrors = true;
        }
    }

    // Skip power and voltage checks during emergency wind stow
    if (!_windStowActive) {
        // Check power consumption (only when NOT in emergency wind stow)
        float powerValue = ina219Manager.getPower();
        if (powerValue > getMaxPowerBeforeFault()) overPowerFault = true;

        if (overPowerFault) {
            global_fault = true;
            errorText += "Power exceeded " + String(getMaxPowerBeforeFault()) + "W. Rotator may be stuck or jammed. Power: " + String(powerValue) + "W\n";
            hasNewErrors = true;
        }

        // Check voltage level (only when NOT in emergency wind stow)
        float loadVoltageValue = ina219Manager.getLoadVoltage();
        if (loadVoltageValue < getMinVoltageThreshold()) lowVoltageFault = true;

        if (lowVoltageFault) {
            global_fault = true;
            errorText += "Voltage too low. Voltage: " + String(loadVoltageValue) + "V\n";
            hasNewErrors = true;
        }
    } else {
        // During emergency wind stow, log power consumption but don't fault
        float powerValue = ina219Manager.getPower();
        float loadVoltageValue = ina219Manager.getLoadVoltage();
        
        static unsigned long lastStowPowerLog = 0;
        if (millis() - lastStowPowerLog > 2000) { // Log every 2 seconds during stow
            _logger.info("EMERGENCY STOW - Power: " + String(powerValue, 1) + "W, Voltage: " + 
                        String(loadVoltageValue, 1) + "V (safety limits bypassed)");
            lastStowPowerLog = millis();
        }
        
        // Reset power/voltage faults during emergency stow to allow movement
        overPowerFault = false;
        lowVoltageFault = false;
    }

    // Check error convergence safety (only when not in calibration mode or emergency stow)
    if (!calMode && !_windStowActive) {
        checkErrorConvergence();
        
        if (errorDivergenceFault) {
            global_fault = true;
            errorText += "MOTOR ERROR DIVERGENCE DETECTED. Errors are increasing instead of decreasing.\n";
            hasNewErrors = true;
        }
    }

    // Emergency stop on fault (except in calibration mode, wind stow mode, and wind tracking mode)
    if (global_fault && !calMode && !_windStowActive && !_windTrackingActive) {
        setPWM(_pwm_pin_el, 255);
        setPWM(_pwm_pin_az, 255);
        errorText += "EMERGENCY ALL STOP. RESTART ESP32 TO CLEAR FAULTS.\n";
        hasNewErrors = true;
        slowPrint(errorText, 0);
    }
}


// =============================================================================
// WIND SAFETY METHODS
// =============================================================================

void MotorSensorController::updateWindStowStatus() {
    if (_weatherPoller == nullptr) {
        return;
    }
    
    // Update at regular intervals
    unsigned long currentTime = millis();
    if (currentTime - _lastWindStowUpdate < WIND_STOW_UPDATE_INTERVAL) {
        return;
    }
    _lastWindStowUpdate = currentTime;
    
    // Check if emergency stow should be active
    if (_weatherPoller->shouldActivateEmergencyStow()) {
        auto windSafetyData = _weatherPoller->getWindSafetyData();
        setWindStowActive(true, windSafetyData.stowReason, windSafetyData.currentStowDirection);
        
        // Perform the actual stow positioning
        performWindStow();
    } else {
        setWindStowActive(false, "", 0.0);
    }
}

void MotorSensorController::setWindStowActive(bool active, const String& reason, float direction) {
    if (_windStowMutex != NULL && xSemaphoreTake(_windStowMutex, portMAX_DELAY) == pdTRUE) {
        bool wasActive = _windStowActive;
        
        _windStowActive = active;
        _windStowReason = reason;
        _windStowDirection = direction;
        
        if (active && !wasActive) {
            _logger.warn("EMERGENCY WIND STOW ACTIVATED: " + reason + 
                       " - Moving to safe direction: " + String(direction, 1) + "°");
            _logger.warn("POWER SAFETY OVERRIDES ENABLED - Power and voltage limits bypassed for emergency stow");
            _logger.warn("Using emergency motor gains - AZ P=" + String(EMERGENCY_STOW_P_AZ) + 
                       ", EL P=" + String(EMERGENCY_STOW_P_EL));
            
            // Clear any existing power/voltage faults to allow emergency movement
            overPowerFault = false;
            lowVoltageFault = false;
            
        } else if (!active && wasActive) {
            _logger.info("Emergency wind stow deactivated - normal operation and safety limits resumed");
        }
        
        xSemaphoreGive(_windStowMutex);
    }
}

void MotorSensorController::performWindStow() {
    if (!_windStowActive) {
        return;
    }
    
    // Set the dish to the wind-safe position (edge-on to wind)
    if (_windStowMutex != NULL && xSemaphoreTake(_windStowMutex, portMAX_DELAY) == pdTRUE) {
        float stowAz = _windStowDirection;
        float stowEl = 0.0; // Lower elevation for wind safety
        
        // Override normal setpoints for wind stow (use internal methods to avoid manual tracking)
        setSetPointAzInternal(stowAz);
        setSetPointElInternal(stowEl);
        
        xSemaphoreGive(_windStowMutex);
    }
}

bool MotorSensorController::shouldBlockMovement() {
    // Block external movement commands when in wind stow mode
    // Allow internal wind stow movements to continue
    return _windStowActive && !calMode;
}

bool MotorSensorController::isWindStowActive() {
    return _windStowActive.load();
}

String MotorSensorController::getWindStowReason() {
    String reason = "";
    if (_windStowMutex != NULL && xSemaphoreTake(_windStowMutex, portMAX_DELAY) == pdTRUE) {
        reason = _windStowReason;
        xSemaphoreGive(_windStowMutex);
    }
    return reason;
}

float MotorSensorController::getWindStowDirection() {
    float direction = 0.0;
    if (_windStowMutex != NULL && xSemaphoreTake(_windStowMutex, portMAX_DELAY) == pdTRUE) {
        direction = _windStowDirection;
        xSemaphoreGive(_windStowMutex);
    }
    return direction;
}

bool MotorSensorController::isMovementBlocked() {
    return shouldBlockMovement();
}

// =============================================================================
// WIND TRACKING METHODS
// =============================================================================

void MotorSensorController::updateWindTrackingStatus() {
    if (_weatherPoller == nullptr) {
        return;
    }
    
    unsigned long currentTime = millis();
    
    // Update wind tracking at regular intervals (every 10 seconds)
    if (currentTime - _lastWindTrackingUpdate < WIND_TRACKING_UPDATE_INTERVAL) {
        return;
    }
    _lastWindTrackingUpdate = currentTime;
    
    // Check if wind tracking should be active
    bool shouldActivate = shouldActivateWindTracking();
    
    if (shouldActivate && !_windTrackingActive) {
        _logger.info(">>> ACTIVATING wind tracking - 60 second timeout reached <<<");
        setWindTrackingActive(true);
        // Immediately perform wind tracking to move to current position
        _logger.debug("Calling performWindTracking() immediately after activation");
        performWindTracking();
    } else if (!shouldActivate && _windTrackingActive) {
        _logger.info(">>> DEACTIVATING wind tracking - conditions no longer met <<<");
        setWindTrackingActive(false);
    } else if (_windTrackingActive) {
        // Continue normal wind tracking
        _logger.debug("Continuing wind tracking (already active)");
        performWindTracking();
    }
}


bool MotorSensorController::shouldActivateWindTracking() {
    if (_weatherPoller == nullptr) {
        _logger.debug("Wind tracking blocked: No weather poller");
        return false;
    }
    
    if (!_weatherPoller->isWindBasedHomeEnabled()) {
        _logger.debug("Wind tracking blocked: Wind-based home not enabled in settings");
        return false;
    }
    
    if (_windStowActive) {
        _logger.debug("Wind tracking blocked: Emergency wind stow active");
        return false;
    }
    
    if (calMode) {
        _logger.debug("Wind tracking blocked: Calibration mode active");
        return false;
    }
    
    unsigned long currentTime = millis();
    unsigned long timeSinceManual = currentTime - _lastManualSetpointTime;
    
    if (timeSinceManual < MANUAL_SETPOINT_TIMEOUT) {
        unsigned long remainingTime = MANUAL_SETPOINT_TIMEOUT - timeSinceManual;
        _logger.debug("Wind tracking blocked: Manual timeout not reached (" + 
                     String(timeSinceManual/1000) + "s elapsed, " + 
                     String(remainingTime/1000) + "s remaining)");
        return false;
    }
    
    if (!_weatherPoller->isDataValid()) {
        _logger.debug("Wind tracking blocked: Weather data not valid");
        String error = _weatherPoller->getLastError();
        if (error.length() > 0) {
            _logger.debug("  Weather error: " + error);
        }
        return false;
    }
    
    _logger.debug("Wind tracking CAN activate - all conditions met");
    return true;
}

void MotorSensorController::setWindTrackingActive(bool active) {
    bool wasActive = _windTrackingActive.load();
    _windTrackingActive = active;
    
    if (active && !wasActive) {
        // Reset the last direction to force movement on first activation
        _lastWindTrackingDirection = -999.0;  // Invalid direction to force first update
        _logger.info("Wind tracking ACTIVATED - will move to current wind home position");
        _logger.debug("Reset last wind direction to force initial movement");
    } else if (!active && wasActive) {
        _logger.info("Wind tracking DEACTIVATED");
    }
}

void MotorSensorController::performWindTracking() {
    if (!_windTrackingActive || _weatherPoller == nullptr) {
        return;
    }
    
    // Get current weather data
    WeatherData weatherData = _weatherPoller->getWeatherData();
    if (!weatherData.dataValid) {
        _logger.debug("Wind tracking skipped: Weather data not valid");
        return;
    }
    
    // Calculate optimal direction based on current wind
    float optimalDirection = _weatherPoller->calculateOptimalStowDirection(weatherData.currentWindDirection);
    
    _logger.debug("Wind tracking check - Current wind: " + String(weatherData.currentWindDirection, 1) + 
                 "°, Optimal: " + String(optimalDirection, 1) + 
                 "°, Last: " + String(_lastWindTrackingDirection, 1) + "°");
    
    // Always move to the current optimal wind position (no threshold check)
    if (optimalDirection != _lastWindTrackingDirection) {
        float directionChange = abs(optimalDirection - _lastWindTrackingDirection);
        
        String reason = (_lastWindTrackingDirection == -999.0) ? 
            "INITIAL wind home positioning" : 
            "Wind direction change (" + String(directionChange, 1) + "°)";
        
        _logger.info("WIND TRACKING UPDATE - " + reason);
        _logger.info("  Current wind direction: " + String(weatherData.currentWindDirection, 1) + "°");
        _logger.info("  Optimal dish direction: " + String(optimalDirection, 1) + "°");
        _logger.info("  Previous dish direction: " + String(_lastWindTrackingDirection, 1) + "°");
        
        // Update setpoints using internal methods to avoid triggering manual command tracking
        setSetPointAzInternal(optimalDirection);
        setSetPointElInternal(0.0);  // Keep elevation at 0 for wind tracking
        
        _lastWindTrackingDirection = optimalDirection;
        _logger.info("  New setpoints: Az=" + String(optimalDirection, 1) + "°, El=0.0°");
        
    } else {
        _logger.debug("Wind tracking: No movement needed (direction unchanged)");
    }
}

bool MotorSensorController::isWindTrackingActive() {
    return _windTrackingActive.load();
}

String MotorSensorController::getWindTrackingStatus() {
    if (!_windTrackingActive) {
        return "Inactive";
    }
    
    if (_weatherPoller == nullptr || !_weatherPoller->isDataValid()) {
        return "Active (No weather data)";
    }
    
    WeatherData weatherData = _weatherPoller->getWeatherData();
    return "Active - Wind: " + String(weatherData.currentWindDirection, 1) + 
           "°, Target: " + String(_lastWindTrackingDirection, 1) + "°";
}

// =============================================================================
// MODIFIED SETPOINT METHODS TO HANDLE WIND STOW AND TRACKING
// =============================================================================

void MotorSensorController::setSetPointAz(float value) {
    // Block external setpoint changes during wind stow (except during calibration)
    if (_windStowActive && !calMode) {
        _logger.warn("Azimuth setpoint change blocked - wind stow active");
        return;
    }
    
    // Record manual setpoint command time and deactivate wind tracking
    _lastManualSetpointTime = millis();
    
    _logger.info("MANUAL AZ command: " + String(value, 2) + "°");
    if (_weatherPoller != nullptr && _weatherPoller->isWindBasedHomeEnabled()) {
        _logger.info("  Wind home will activate in " + String(MANUAL_SETPOINT_TIMEOUT/1000) + " seconds");
    }
    
    if (_windTrackingActive) {
        _logger.info("  Deactivating wind tracking due to manual command");
        setWindTrackingActive(false);
    }
    
    setSetPointAzInternal(value);
}

void MotorSensorController::setSetPointEl(float value) {
    // Block external setpoint changes during wind stow (except during calibration)
    if (_windStowActive && !calMode) {
        _logger.warn("Elevation setpoint change blocked - wind stow active");
        return;
    }
    
    // Record manual setpoint command time and deactivate wind tracking
    _lastManualSetpointTime = millis();
    
    _logger.info("MANUAL EL command: " + String(value, 2) + "°");
    if (_weatherPoller != nullptr && _weatherPoller->isWindBasedHomeEnabled()) {
        _logger.info("  Wind home will activate in " + String(MANUAL_SETPOINT_TIMEOUT/1000) + " seconds");
    }
    
    if (_windTrackingActive) {
        _logger.info("  Deactivating wind tracking due to manual command");
        setWindTrackingActive(false);
    }
    
    setSetPointElInternal(value);
}


void MotorSensorController::setSetPointAzInternal(float value) {
    if (_setPointMutex != NULL && xSemaphoreTake(_setPointMutex, portMAX_DELAY) == pdTRUE) {
        _setpoint_az = value;
        _setPointAzUpdated = true;
        xSemaphoreGive(_setPointMutex);
    }
}

void MotorSensorController::setSetPointElInternal(float value) {
    if (_setPointMutex != NULL && xSemaphoreTake(_setPointMutex, portMAX_DELAY) == pdTRUE) {
        _setpoint_el = value;
        _setPointElUpdated = true;
        xSemaphoreGive(_setPointMutex);
    }
}

// =============================================================================
// ERROR CONVERGENCE SAFETY METHODS (unchanged from original)
// =============================================================================

void MotorSensorController::updateErrorTracking() {
    unsigned long currentTime = millis();
    
    // Update azimuth error tracking
    if (currentTime - _azErrorTracker.lastSampleTime >= ERROR_SAMPLE_INTERVAL) {
        _azErrorTracker.errorHistory[_azErrorTracker.currentIndex] = abs(getErrorAz());
        _azErrorTracker.timestamps[_azErrorTracker.currentIndex] = currentTime;
        _azErrorTracker.currentIndex = (_azErrorTracker.currentIndex + 1) % ERROR_HISTORY_SIZE;
        _azErrorTracker.sampleCount = min(_azErrorTracker.sampleCount + 1, ERROR_HISTORY_SIZE);
        _azErrorTracker.lastSampleTime = currentTime;
        _azErrorTracker.motorShouldBeActive = (setPointState_az && !_isAzMotorLatched && !global_fault);
    }
    
    // Update elevation error tracking
    if (currentTime - _elErrorTracker.lastSampleTime >= ERROR_SAMPLE_INTERVAL) {
        _elErrorTracker.errorHistory[_elErrorTracker.currentIndex] = abs(getErrorEl());
        _elErrorTracker.timestamps[_elErrorTracker.currentIndex] = currentTime;
        _elErrorTracker.currentIndex = (_elErrorTracker.currentIndex + 1) % ERROR_HISTORY_SIZE;
        _elErrorTracker.sampleCount = min(_elErrorTracker.sampleCount + 1, ERROR_HISTORY_SIZE);
        _elErrorTracker.lastSampleTime = currentTime;
        _elErrorTracker.motorShouldBeActive = (setPointState_el && !_isElMotorLatched && !global_fault);
    }
}

void MotorSensorController::checkStall() {
    unsigned long currentTime = millis();
    _jitterAzMotors = false;
    _jitterElMotors = false;

    // Check azimuth convergence
    if (_azErrorTracker.sampleCount >= ERROR_HISTORY_SIZE / 2 && 
        _azErrorTracker.motorShouldBeActive &&
        (currentTime - _azErrorTracker.setpointChangeTime) > CONVERGENCE_TIMEOUT) {

        // Attempt to jitter the motor out of stall protection
        if (isConvergenceStalled(_azErrorTracker, _MIN_AZ_TOLERANCE)) {
            _jitterAzMotors = true;
        }
    }

    if (_elErrorTracker.sampleCount >= ERROR_HISTORY_SIZE / 2 && 
        _elErrorTracker.motorShouldBeActive &&
        (currentTime - _elErrorTracker.setpointChangeTime) > CONVERGENCE_TIMEOUT) {
        
        if (isConvergenceStalled(_elErrorTracker, _MIN_EL_TOLERANCE)) {
            _jitterElMotors = true;
        }
    }
}

void MotorSensorController::checkErrorConvergence() {
    // Only check convergence if we have enough data and sufficient time has passed since setpoint changes
    unsigned long currentTime = millis();
    
    // Check azimuth convergence
    if (_azErrorTracker.sampleCount >= ERROR_HISTORY_SIZE / 2 && 
        _azErrorTracker.motorShouldBeActive &&
        (currentTime - _azErrorTracker.setpointChangeTime) > CONVERGENCE_TIMEOUT) {
        
        if (isErrorDiverging(_azErrorTracker, _MIN_AZ_TOLERANCE)) {
            if (!errorDivergenceFault) {
                _logger.error("AZ Error divergence detected");
                errorDivergenceFault = true;
            }
        }
    }
    
    // Check elevation convergence
    if (_elErrorTracker.sampleCount >= ERROR_HISTORY_SIZE / 2 && 
        _elErrorTracker.motorShouldBeActive &&
        (currentTime - _elErrorTracker.setpointChangeTime) > CONVERGENCE_TIMEOUT) {
        
        if (isErrorDiverging(_elErrorTracker, _MIN_EL_TOLERANCE)) {
            if (!errorDivergenceFault) {
                _logger.error("EL Error divergence detected");
                errorDivergenceFault = true;
            }
        }
    }
}

bool MotorSensorController::isErrorDiverging(const ErrorTracker& tracker, float tolerance) {
    if (tracker.sampleCount < ERROR_HISTORY_SIZE / 2) return false;
    
    // Get recent error values
    int recentSamples = min(tracker.sampleCount, ERROR_HISTORY_SIZE / 3);
    float recentSum = 0;
    float oldSum = 0;
    int oldSamples = 0;
    
    // Calculate average of recent samples
    for (int i = 0; i < recentSamples; i++) {
        int idx = (tracker.currentIndex - 1 - i + ERROR_HISTORY_SIZE) % ERROR_HISTORY_SIZE;
        recentSum += tracker.errorHistory[idx];
    }
    float recentAvg = recentSum / recentSamples;
    
    // Calculate average of older samples
    for (int i = recentSamples; i < tracker.sampleCount; i++) {
        int idx = (tracker.currentIndex - 1 - i + ERROR_HISTORY_SIZE) % ERROR_HISTORY_SIZE;
        oldSum += tracker.errorHistory[idx];
        oldSamples++;
    }
    
    if (oldSamples == 0) return false;
    float oldAvg = oldSum / oldSamples;
    
    // Check if error is diverging (recent average is significantly larger than old average)
    bool isDiverging = (recentAvg > oldAvg * DIVERGENCE_THRESHOLD) && (recentAvg > tolerance * 2);
    
    if (isDiverging) {
        _logger.debug("Divergence detected - Recent avg: " + String(recentAvg, 3) + 
                     ", Old avg: " + String(oldAvg, 3) + ", Tolerance: " + String(tolerance, 3));
    }
    
    return isDiverging;
}

bool MotorSensorController::isConvergenceStalled(const ErrorTracker& tracker, float tolerance) {
    if (tracker.sampleCount < ERROR_HISTORY_SIZE / 2) return false;
    
    // Calculate the rate of change in error
    float changeRate = calculateErrorChangeRate(tracker);
    
    // Get current error
    int currentIdx = (tracker.currentIndex - 1 + ERROR_HISTORY_SIZE) % ERROR_HISTORY_SIZE;
    float currentError = tracker.errorHistory[currentIdx];
    
    // Check if convergence is stalled:
    // 1. Current error is above tolerance (motor should be active)
    // 2. Rate of change is too small (not making progress)
    bool isStalled = (currentError > tolerance * 1.5) && (abs(changeRate) < STALL_THRESHOLD);
    
    if (isStalled) {
        _logger.debug("Stall detected - Current error: " + String(currentError, 3) + 
                     ", Change rate: " + String(changeRate, 4) + " deg/s, Tolerance: " + String(tolerance, 3));
    }
    
    return isStalled;
}

float MotorSensorController::calculateErrorChangeRate(const ErrorTracker& tracker) {
    if (tracker.sampleCount < 3) return 0;
    
    // Use linear regression to calculate the slope (rate of change)
    int samples = min(tracker.sampleCount, ERROR_HISTORY_SIZE / 2);
    float sumX = 0, sumY = 0, sumXY = 0, sumX2 = 0;
    
    for (int i = 0; i < samples; i++) {
        int idx = (tracker.currentIndex - 1 - i + ERROR_HISTORY_SIZE) % ERROR_HISTORY_SIZE;
        float x = i; // Time index (relative)
        float y = tracker.errorHistory[idx];
        
        sumX += x;
        sumY += y;
        sumXY += x * y;
        sumX2 += x * x;
    }
    
    // Calculate slope using least squares method
    float slope = (samples * sumXY - sumX * sumY) / (samples * sumX2 - sumX * sumX);
    
    // Convert to degrees per second (slope is in degrees per sample, multiply by sample rate)
    return slope * (1000.0 / ERROR_SAMPLE_INTERVAL);
}

void MotorSensorController::resetErrorTracker(ErrorTracker& tracker) {
    tracker.currentIndex = 0;
    tracker.sampleCount = 0;
    tracker.lastSampleTime = 0;
    tracker.setpointChangeTime = millis();
    tracker.motorShouldBeActive = false;
    memset(tracker.errorHistory, 0, sizeof(tracker.errorHistory));
    memset(tracker.timestamps, 0, sizeof(tracker.timestamps));
}

// =============================================================================
// MOTOR CONTROL (unchanged from original)
// =============================================================================

void MotorSensorController::actuate_motor_az(int MIN_SPEED) {
    // Use emergency high P gain during wind stow for maximum torque
    int effectiveP_az = _windStowActive ? EMERGENCY_STOW_P_AZ : P_az;
    double error = getErrorAz() * effectiveP_az;

    // Set direction based on error sign
    digitalWrite(_ccw_pin_az, (error >= 0) ? LOW : HIGH);

    // Calculate target speed with constraints
    int targetSpeed = MIN_SPEED - constrain(abs(error), _maxAdjustedSpeed_az, MIN_SPEED);
    targetSpeed = max(targetSpeed, _maxAdjustedSpeed_az);

    // Reset speed on direction change
    if ((error < 0) != (_last_error_az < 0)) {
        _current_speed_az = MIN_SPEED;
    }
    _last_error_az = error;

    // Control motor speed
    if (setPointState_az && !global_fault && !_isAzMotorLatched) {
        // During emergency stow, use more aggressive speed changes
        int speedDecrement = _windStowActive ? 20 : 10;  // Faster speed ramp during stow
        
        if (_current_speed_az > targetSpeed) {
            _current_speed_az = max(_current_speed_az - speedDecrement, targetSpeed);
        } else if (_current_speed_az < targetSpeed) {
            _current_speed_az = min(_current_speed_az + speedDecrement, targetSpeed);
        }
        setPWM(_pwm_pin_az, _current_speed_az);

        if(_jitterAzMotors) {
            _logger.info("Attempting recovery of stalled AZ motor with jitter");
            setPWM(_pwm_pin_az, 0);
            digitalWrite(_ccw_pin_az, (error >= 0) ? HIGH : LOW);  // Brief opposite direction
            delayMicroseconds(150000);
            digitalWrite(_ccw_pin_az, (error >= 0) ? LOW : HIGH);
            delayMicroseconds(150000);
            setPWM(_pwm_pin_az, _current_speed_az);
        }
    } else {
        // Stop motor
        setPWM(_pwm_pin_az, 255);
        _current_speed_az = MIN_SPEED;
    }
}


void MotorSensorController::actuate_motor_el(int MIN_SPEED) {
    // Use emergency high P gain during wind stow for maximum torque
    int effectiveP_el = _windStowActive ? EMERGENCY_STOW_P_EL : P_el;
    double error = getErrorEl() * effectiveP_el;

    // Set direction based on error sign
    digitalWrite(_ccw_pin_el, (error >= 0) ? LOW : HIGH);

    // Calculate target speed with constraints
    int targetSpeed = MIN_SPEED - constrain(abs(error), _maxAdjustedSpeed_el, MIN_SPEED);
    targetSpeed = max(targetSpeed, _maxAdjustedSpeed_el);

    // Reset speed on direction change
    if ((error < 0) != (_last_error_el < 0)) {
        _current_speed_el = MIN_SPEED;
    }
    _last_error_el = error;

    // Control motor speed
    if (setPointState_el && !global_fault && !_isElMotorLatched) {
        // During emergency stow, use more aggressive speed changes
        int speedDecrement = _windStowActive ? 20 : 10;  // Faster speed ramp during stow
        
        if (_current_speed_el > targetSpeed) {
            _current_speed_el = max(_current_speed_el - speedDecrement, targetSpeed);
        } else if (_current_speed_el < targetSpeed) {
            _current_speed_el = min(_current_speed_el + speedDecrement, targetSpeed);
        }
        setPWM(_pwm_pin_el, _current_speed_el);

        if(_jitterElMotors) {
            _logger.info("Attempting recovery of stalled EL motor with jitter");
            setPWM(_pwm_pin_el, 0);
            digitalWrite(_ccw_pin_el, (error >= 0) ? HIGH : LOW);  // Brief opposite direction
            delayMicroseconds(150000);
            digitalWrite(_ccw_pin_el, (error >= 0) ? LOW : HIGH);
            delayMicroseconds(150000);
            setPWM(_pwm_pin_el, _current_speed_el);
        }
    } else {
        // Stop motor
        setPWM(_pwm_pin_el, 255);
        _current_speed_el = MIN_SPEED;
    }
}

void MotorSensorController::updateMotorControl(float currentSetPointAz, float currentSetPointEl, bool setPointAzUpdated, bool setPointElUpdated) {
    // Determine if motors should be active based on error tolerance
    setPointState_az = (fabs(getErrorAz()) > _MIN_AZ_TOLERANCE);
    setPointState_el = (fabs(getErrorEl()) > _MIN_EL_TOLERANCE);

    // Reset latch parameters on setpoint changes
    if (setPointAzUpdated) {
        _isAzMotorLatched = false;
        _prev_error_az = 0;
        resetErrorTracker(_azErrorTracker);
        errorDivergenceFault = false;  // Clear convergence faults on new setpoint
    }

    if (setPointElUpdated) {
        _isElMotorLatched = false;
        _prev_error_el = 0;
        resetErrorTracker(_elErrorTracker);
        errorDivergenceFault = false;  // Clear convergence faults on new setpoint
    }

    // Detect overshoot (error sign flip)
    bool az_sign_flipped = (_prev_error_az * getErrorAz() < 0) && 
                          (fabs(_prev_error_az) > 0.0001) && 
                          (fabs(getErrorAz()) > 0.0001);
    bool el_sign_flipped = (_prev_error_el * getErrorEl() < 0) && 
                          (fabs(_prev_error_el) > 0.0001) && 
                          (fabs(getErrorEl()) > 0.0001);

    // Latch motors on target reached or overshoot
    if (!setPointState_az || az_sign_flipped) {
        _isAzMotorLatched = true;
    }

    if (!setPointState_el || el_sign_flipped) {
        _isElMotorLatched = true;
    }

    _prev_error_az = getErrorAz();
    _prev_error_el = getErrorEl();
}

void MotorSensorController::updateMotorPriority(bool setPointAzUpdated, bool setPointElUpdated) {
    // Handle single motor mode priority
    if (singleMotorMode) {
        // Determine priority based on normalized error when setpoint changes
        if (setPointAzUpdated || setPointElUpdated) {
            // Normalize by motor speeds (1.5RPM for az, 0.25RPM for el)
            _az_priority = (fabs(getErrorAz()) / 1.5 > fabs(getErrorEl()) / 0.25);
        }
        
        // Execute priority control
        if (_az_priority) {
            if (setPointState_az) {
                setPointState_el = false;
            } else {
                _az_priority = false;
            }
        } else {
            if (setPointState_el) {
                setPointState_az = false;
            } else {
                _az_priority = true;
            }
        }
    }

    // Adjust maximum speeds based on motor activity
    _maxAdjustedSpeed_az = setPointState_el ? max_dual_motor_az_speed : max_single_motor_az_speed;
    _maxAdjustedSpeed_el = setPointState_az ? max_dual_motor_el_speed : max_single_motor_el_speed;
}

void MotorSensorController::setPWM(int pin, int PWM) {
    analogWrite(pin, PWM);
}

// =============================================================================
// REMAINING METHODS (unchanged from original - angle calculation, sensor reading, etc.)
// =============================================================================

// =============================================================================
// ANGLE CALCULATION AND ERROR HANDLING (unchanged from original)
// =============================================================================

void MotorSensorController::angle_shortest_error_az(float target_angle, float current_angle) {
    // Normalize angles to 0-360 range
    target_angle = fmod(target_angle, 360);
    if (target_angle < 0) target_angle += 360;

    current_angle = fmod(current_angle, 360);
    if (current_angle < 0) current_angle += 360;

    // Handle edge cases at 0/360 boundary based on unwinding state
    if (needs_unwind >= 1 && current_angle < 90) {
        current_angle += 360;
    } else if (needs_unwind <= -1 && current_angle > 270) {
        current_angle -= 360;
    }

    // Calculate shortest path error
    float error = target_angle - current_angle;
    if (error > 180) {
        error -= 360;
    } else if (error < -180) {
        error += 360;
    }

    // Handle unwinding requirements
    if (target_angle == 0 || ((current_angle + error) > 360 || (current_angle + error) < 0)) {
        if (needs_unwind <= -1) {
            error = (error > 180) ? error : error + 360;
        } else if (needs_unwind >= 1) {
            error = (error > -180) ? error - 360 : error;
        }
    }

    setErrorAz(error);
}

void MotorSensorController::angle_error_el(float target_angle, float current_angle) {
    // Normalize angles
    target_angle = fmod(target_angle, 360);
    current_angle = fmod(current_angle, 360);
    
    // Calculate shortest path error
    float error = target_angle - current_angle;
    if (error > 180) {
        error -= 360;
    } else if (error < -180) {
        error += 360;
    }

    setErrorEl(error);
}

float MotorSensorController::correctAngle(float startAngle, float inputAngle) {
    float correctedAngle = inputAngle - startAngle;
    if (correctedAngle < 0) {
        correctedAngle += 360;
    }
    return correctedAngle;
}

void MotorSensorController::calcIfNeedsUnwind(float correctedAngle_az) {
    // Determine current quadrant (1: 0-90, 2: 91-180, 3: 181-270, 4: 271-359)
    _quadrantNumber_az = (correctedAngle_az <= 90) ? 1 :
                        (correctedAngle_az <= 180) ? 2 :
                        (correctedAngle_az <= 270) ? 3 : 4;
    
    // Check for quadrant transitions
    if (_quadrantNumber_az != _previousquadrantNumber_az) {
        if (!calMode) {
            // Handle unwinding logic at 180-degree crossings
            if (_quadrantNumber_az == 2 && _previousquadrantNumber_az == 3) {
                needs_unwind--;
            } else if (_quadrantNumber_az == 3 && _previousquadrantNumber_az == 2) {
                needs_unwind++;
            }
        }
        _previousquadrantNumber_az = _quadrantNumber_az;
    }
}

// =============================================================================
// SENSOR READING AND I2C COMMUNICATION (unchanged from original)
// =============================================================================

float MotorSensorController::getAvgAngle(int i2c_addr) {
    if (_getAngleMutex == NULL || xSemaphoreTake(_getAngleMutex, portMAX_DELAY) != pdTRUE) {
        _logger.error("Failed to take mutex in getAvgAngle");
        badAngleFlag = true;
        return 0;
    }

    // Verify magnet presence before reading
    int magnetStatus = checkMagnetPresence(i2c_addr);
    if ((magnetStatus & 32) == 0) {
        _logger.error("MAGNET WENT MISSING DURING ROUTINE ANGLE READ!");
        magnetFault = true;
    }

    float angles[_numAvg];
    int validReadings = 0;
    int errorCounter = 0;
    const int MAX_ATTEMPTS = _numAvg * 2;
    
    // Collect angle readings
    for (int attempt = 0; attempt < MAX_ATTEMPTS && validReadings < _numAvg; attempt++) {
        float rawAngle = ReadRawAngle(i2c_addr);
        
        if (rawAngle != -999) {
            angles[validReadings++] = rawAngle;
        } else {
            errorCounter++;
            if (errorCounter > _numAvg) break;
        }
        
        delayMicroseconds(100);
    }

    // Handle insufficient readings
    if (validReadings == 0) {
        xSemaphoreGive(_getAngleMutex);
        _logger.error("Failed to get any valid angle readings");
        badAngleFlag = true;
        return 0;
    }

    float angleMean = calculateAngleMeanWithDiscard(angles, validReadings);
    xSemaphoreGive(_getAngleMutex);
    
    return angleMean;
}

float MotorSensorController::ReadRawAngle(int i2c_addr) {
    uint8_t buffer[2];
    const unsigned long timeout = 3000;

    // Request angle data
    Wire.beginTransmission(i2c_addr);
    Wire.write(0x0C);
    byte error = Wire.endTransmission(false);

    if (error != 0) {
        _logger.error("I2C error during transmission to sensor 0x" + String(i2c_addr, HEX) + ": " + String(error));
        updateI2CErrorCounter(i2c_addr);
        return -999;
    }

    delayMicroseconds(25);
    byte bytesReceived = Wire.requestFrom(i2c_addr, 2);

    if (bytesReceived != 2) {
        _logger.error("I2C error: Requested 2 bytes but received " + String(bytesReceived) + " from 0x" + String(i2c_addr, HEX));
        updateI2CErrorCounter(i2c_addr);
        return -999;
    }

    resetI2CErrorCounter(i2c_addr);

    // Timeout check
    unsigned long startTime = millis();
    while (Wire.available() < 2) {
        if (millis() - startTime > timeout) {
            _logger.error("Timeout waiting for bytes from hall sensor");
            return -999;
        }
    }

    // Read and process data
    Wire.readBytes(buffer, 2);
    word rawAngleValue = ((uint16_t)buffer[0] << 8) | buffer[1];
    return rawAngleValue * 0.087890625; // Convert to degrees
}

int MotorSensorController::checkMagnetPresence(int i2c_addr) {
    int magnetStatus = 0;
    byte error;
    
    while (true) {
        Wire.beginTransmission(i2c_addr);
        Wire.write(0x0B); // Status register
        error = Wire.endTransmission();
        
        if (error != 0) {
            _logger.error("I2C error during transmission to sensor 0x" + String(i2c_addr, HEX) + ": " + String(error));
            updateI2CErrorCounter(i2c_addr);
            
            // Check consecutive error limit
            if ((i2c_addr == _az_hall_i2c_addr && _consecutivei2cErrors_az > MAX_CONSECUTIVE_ERRORS) ||
                (i2c_addr == _el_hall_i2c_addr && _consecutivei2cErrors_el > MAX_CONSECUTIVE_ERRORS)) {
                if (i2c_addr == _az_hall_i2c_addr) i2cErrorFlag_az = true;
                else i2cErrorFlag_el = true;
                return -999;
            }
            continue;
        }
        
        Wire.requestFrom(i2c_addr, 1);
        while (Wire.available() == 0);
        magnetStatus = Wire.read();
        break;
    }

    resetI2CErrorCounter(i2c_addr);
    return magnetStatus;
}

float MotorSensorController::calculateAngleMeanWithDiscard(float* array, int size) {
    float x[size], y[size];

    // Convert angles to unit vectors
    for (int i = 0; i < size; i++) {
        float angleRad = array[i] * PI / 180.0;
        x[i] = cos(angleRad);
        y[i] = sin(angleRad);
    }

    // Calculate means
    float xSum = 0, ySum = 0;
    for (int i = 0; i < size; i++) {
        xSum += x[i];
        ySum += y[i];
    }
    float xMean = xSum / size;
    float yMean = ySum / size;

    // Calculate standard deviations
    float sumX = 0, sumY = 0;
    for (int i = 0; i < size; i++) {
        sumX += pow(x[i] - xMean, 2);
        sumY += pow(y[i] - yMean, 2);
    }
    float stdDevX = sqrt(sumX / (size > 1 ? size - 1 : 1));
    float stdDevY = sqrt(sumY / (size > 1 ? size - 1 : 1));

    // Calculate final mean excluding outliers
    xSum = ySum = 0;
    for (int i = 0; i < size; i++) {
        if ((fabs(x[i] - xMean) <= 2.0 * stdDevX) && (fabs(y[i] - yMean) <= 2.0 * stdDevY)) {
            xSum += x[i];
            ySum += y[i];
        }
    }

    return atan2(ySum, xSum) * 180.0 / PI;
}

// =============================================================================
// SETPOINT AND ANGLE ACCESS METHODS (updated with wind stow checks)
// =============================================================================

float MotorSensorController::getSetPointAz() {
    float result = 0;
    if (_setPointMutex != NULL && xSemaphoreTake(_setPointMutex, portMAX_DELAY) == pdTRUE) {
        result = _setpoint_az;
        xSemaphoreGive(_setPointMutex);
    }
    return result;
}

float MotorSensorController::getSetPointEl() {
    float result = 0;
    if (_setPointMutex != NULL && xSemaphoreTake(_setPointMutex, portMAX_DELAY) == pdTRUE) {
        result = _setpoint_el;
        xSemaphoreGive(_setPointMutex);
    }
    return result;
}

void MotorSensorController::setCorrectedAngleAz(float value) {
    if (_correctedAngleMutex != NULL && xSemaphoreTake(_correctedAngleMutex, portMAX_DELAY) == pdTRUE) {
        _correctedAngle_az = value;
        xSemaphoreGive(_correctedAngleMutex);
    }
}

void MotorSensorController::setCorrectedAngleEl(float value) {
    if (_correctedAngleMutex != NULL && xSemaphoreTake(_correctedAngleMutex, portMAX_DELAY) == pdTRUE) {
        _correctedAngle_el = value;
        xSemaphoreGive(_correctedAngleMutex);
    }
}

float MotorSensorController::getCorrectedAngleAz() {
    float result = 0;
    if (_correctedAngleMutex != NULL && xSemaphoreTake(_correctedAngleMutex, portMAX_DELAY) == pdTRUE) {
        result = _correctedAngle_az;
        xSemaphoreGive(_correctedAngleMutex);
    }
    return result;
}

float MotorSensorController::getCorrectedAngleEl() {
    float result = 0;
    if (_correctedAngleMutex != NULL && xSemaphoreTake(_correctedAngleMutex, portMAX_DELAY) == pdTRUE) {
        result = _correctedAngle_el;
        xSemaphoreGive(_correctedAngleMutex);
    }
    return result;
}

double MotorSensorController::getErrorAz() {
    double result = 0;
    if (_errorMutex != NULL && xSemaphoreTake(_errorMutex, portMAX_DELAY) == pdTRUE) {
        result = _error_az;
        xSemaphoreGive(_errorMutex);
    }
    return result;
}

void MotorSensorController::setErrorAz(float value) {
    if (_errorMutex != NULL && xSemaphoreTake(_errorMutex, portMAX_DELAY) == pdTRUE) {
        _error_az = value;
        xSemaphoreGive(_errorMutex);
    }
}

double MotorSensorController::getErrorEl() {
    double result = 0;
    if (_errorMutex != NULL && xSemaphoreTake(_errorMutex, portMAX_DELAY) == pdTRUE) {
        result = _error_el;
        xSemaphoreGive(_errorMutex);
    }
    return result;
}

void MotorSensorController::setErrorEl(float value) {
    if (_errorMutex != NULL && xSemaphoreTake(_errorMutex, portMAX_DELAY) == pdTRUE) {
        _error_el = value;
        xSemaphoreGive(_errorMutex);
    }
}

float MotorSensorController::getElStartAngle() {
    float result = 0;
    if (_el_startAngleMutex != NULL && xSemaphoreTake(_el_startAngleMutex, portMAX_DELAY) == pdTRUE) {
        result = _el_startAngle;
        xSemaphoreGive(_el_startAngleMutex);
    }
    return result;
}

void MotorSensorController::setElStartAngle(float value) {
    if (_el_startAngleMutex != NULL && xSemaphoreTake(_el_startAngleMutex, portMAX_DELAY) == pdTRUE) {
        _preferences.putFloat("el_cal", value);
        _el_startAngle = value;
        xSemaphoreGive(_el_startAngleMutex);
    }
}

int MotorSensorController::getMinVoltageThreshold() {
    return _minVoltageThreshold;
}

void MotorSensorController::setMinVoltageThreshold(int value) {
    if (value > 0 && value < 20) {
        _minVoltageThreshold = value;
        _preferences.putInt("MIN_VOLTAGE", value);
        _logger.info("MIN_VOLTAGE_THRESHOLD set to: " + String(value) + "V");
    }
}

int MotorSensorController::getMaxPowerBeforeFault() {
    return _maxPowerBeforeFault;
}

void MotorSensorController::setMaxPowerBeforeFault(int value) {
    if (value > 0 && value < 25) {
        _maxPowerBeforeFault = value;
        _preferences.putInt("MAX_POWER", value);
    }
}

// =============================================================================
// CONFIGURATION PARAMETER SETTERS (unchanged from original)
// =============================================================================

void MotorSensorController::setPEl(int value) {
    if (value >= -1000 && value <= 1000) {
        P_el = value;
        _preferences.putInt("P_el", value);
        _logger.info("P_el set to: " + String(value));
    }
}

void MotorSensorController::setPAz(int value) {
    if (value >= -1000 && value <= 1000) {
        P_az = value;
        _preferences.putInt("P_az", value);
        _logger.info("P_az set to: " + String(value));
    }
}

void MotorSensorController::setMinElSpeed(int value) {
    if (value >= 0 && value <= 255) {
        MIN_EL_SPEED = value;
        _preferences.putInt("MIN_EL_SPEED", value);
        _logger.info("MIN_EL_SPEED set to: " + String(value));
    }
}

void MotorSensorController::setMinAzSpeed(int value) {
    if (value >= 0 && value <= 255) {
        MIN_AZ_SPEED = value;
        _preferences.putInt("MIN_AZ_SPEED", value);
        _logger.info("MIN_AZ_SPEED set to: " + String(value));
    }
}

void MotorSensorController::setMinAzTolerance(float value) {
    if (value > 0 && value <= 10.0) {
        _MIN_AZ_TOLERANCE = value;
        _preferences.putFloat("MIN_AZ_TOL", value);
        _logger.info("MIN_AZ_TOLERANCE set to: " + String(value));
    }
}

void MotorSensorController::setMinElTolerance(float value) {
    if (value > 0 && value <= 10.0) {
        _MIN_EL_TOLERANCE = value;
        _preferences.putFloat("MIN_EL_TOL", value);
        _logger.info("MIN_EL_TOLERANCE set to: " + String(value));
    }
}

// =============================================================================
// CALIBRATION METHODS (unchanged from original)
// =============================================================================

void MotorSensorController::activateCalMode(bool on) {
    if (on) {
        calMode = true;
        global_fault = false;
        setPWM(_pwm_pin_az, 255);
        setPWM(_pwm_pin_el, 255);
        _logger.info("calMode set to true");
    } else {
        calMode = false;
        _logger.info("calMode set to false");
    }
}

void MotorSensorController::calMoveMotor(const String& runTimeStr, const String& axis) {
    if (!calMode) {
        Serial.println("Calibration mode OFF; ignoring calMove request.");
        return;
    }

    _calRunTime = runTimeStr.toInt();
    _calAxis = axis;
}

void MotorSensorController::calibrate_elevation() {
    if (calMode) {
        float tareAngle = getAvgAngle(_el_hall_i2c_addr);
        setElStartAngle(tareAngle);
        Serial.println("EL CAL DONE");
    }
}

void MotorSensorController::handleCalibrationMode() {
    if (_calState == 0) {
        if (abs(_calRunTime) > 0 && _calAxis != "") {
            _calMoveStartTime = millis();
            _calState = 1;
        } else {
            analogWrite(_pwm_pin_az, 255);
            analogWrite(_pwm_pin_el, 255);
            digitalWrite(_pwm_pin_az, 1);
            digitalWrite(_pwm_pin_el, 1);
        }
    } else if (_calState == 1) {
        int directionPin, pwmPin;
        
        if (_calAxis.equalsIgnoreCase("AZ")) {
            directionPin = _ccw_pin_az;
            pwmPin = _pwm_pin_az;
        } else if (_calAxis.equalsIgnoreCase("EL")) {
            directionPin = _ccw_pin_el;
            pwmPin = _pwm_pin_el;
        }
        
        digitalWrite(directionPin, _calRunTime > 0);
        analogWrite(pwmPin, 0);

        unsigned long elapsedTime = millis() - _calMoveStartTime;
        if (elapsedTime > abs(_calRunTime)) {
            analogWrite(_pwm_pin_az, 255);
            analogWrite(_pwm_pin_el, 255);
            _calRunTime = 0;
            _calAxis = "";
            _calState = 0;
        }
    }
}

// =============================================================================
// UTILITY METHODS (unchanged from original)
// =============================================================================

void MotorSensorController::handleOscillationDetection() {
    // Update needs_unwind in preferences when changed (outside calibration mode)
    if (needs_unwind != _prev_needs_unwind && !calMode) {
        _preferences.putInt("needs_unwind", needs_unwind);
        _prev_needs_unwind = needs_unwind;

        _logger.warn("THIS SHOULD NOT BE RUNNING CONSTANTLY OR THE EEPROM COULD CORRUPT");

        // Start or continue oscillation detection
        if (!_oscillationTimerActive) {
            _oscillationTimerStart = millis();
            _oscillationTimerActive = true;
            _oscillationCount = 1;
            _logger.info("Oscillation detection timer started");
        } else {
            _oscillationCount++;
            _logger.info("Oscillation count: " + String(_oscillationCount));
            
            // Check for excessive oscillation
            if (_oscillationCount >= 10) {
                float currentAngle = getCorrectedAngleAz();
                float newSetpoint = (currentAngle <= 180.0) ? currentAngle - 1 : currentAngle + 1;
                
                _logger.warn("Excessive oscillation detected! Moving " + String((currentAngle <= 180.0) ? "-1°" : "+1°"));
                setSetPointAzInternal(newSetpoint);
                
                _oscillationTimerActive = false;
                _oscillationCount = 0;
            }
        }
    }

    // Reset timer after 60 seconds
    if (_oscillationTimerActive && (millis() - _oscillationTimerStart >= 60000)) {
        _oscillationTimerActive = false;
        _oscillationCount = 0;
        _logger.info("Oscillation detection timer expired, count was: " + String(_oscillationCount));
    }
}

void MotorSensorController::slowPrint(const String& message, int messageID) {
    static unsigned long lastPrintTimes[10] = {0};
    const unsigned long printDelay = 1000;
    
    unsigned long currentTime = millis();
    if (currentTime - lastPrintTimes[messageID] >= printDelay) {
        _logger.error(message);
        lastPrintTimes[messageID] = currentTime;
    }
}

void MotorSensorController::updateI2CErrorCounter(int i2c_addr) {
    if (i2c_addr == _az_hall_i2c_addr) {
        _consecutivei2cErrors_az++;
        if (_consecutivei2cErrors_az >= MAX_CONSECUTIVE_ERRORS) {
            i2cErrorFlag_az = true;
        }
    } else if (i2c_addr == _el_hall_i2c_addr) {
        _consecutivei2cErrors_el++;
        if (_consecutivei2cErrors_el >= MAX_CONSECUTIVE_ERRORS) {
            i2cErrorFlag_el = true;
        }
    }
}

void MotorSensorController::resetI2CErrorCounter(int i2c_addr) {
    if (i2c_addr == _az_hall_i2c_addr) {
        _consecutivei2cErrors_az = 0;
    } else if (i2c_addr == _el_hall_i2c_addr) {
        _consecutivei2cErrors_el = 0;
    }
}

int MotorSensorController::convertPercentageToSpeed(float percentage) {
    return (int)((1 - (percentage / 100.0)) * MIN_AZ_SPEED);
}

int MotorSensorController::convertSpeedToPercentage(float speed) {
    return (int)(100 * (1 - (speed / MIN_AZ_SPEED)));
}

void MotorSensorController::playOdeToJoy() {
    // Stop motors and enter calibration mode temporarily
    setPWM(_pwm_pin_az, 255);
    setPWM(_pwm_pin_el, 255);
    
    bool previousCalMode = calMode;
    activateCalMode(true);
    
    // Musical note frequencies
    const int NOTE_D3 = 147, NOTE_CS4 = 277, NOTE_D4 = 294, NOTE_E4 = 330;
    const int NOTE_FS4 = 370, NOTE_G4 = 392, NOTE_A4 = 440, NOTE_B4 = 494;
    
    // Ode to Joy melody
    const int melody[] = {
        NOTE_E4, NOTE_E4, NOTE_FS4, NOTE_G4, NOTE_G4, NOTE_FS4, NOTE_E4, NOTE_D4,
        NOTE_CS4, NOTE_CS4, NOTE_D4, NOTE_E4, NOTE_E4, NOTE_D4, NOTE_D4,
        NOTE_E4, NOTE_E4, NOTE_FS4, NOTE_G4, NOTE_G4, NOTE_FS4, NOTE_E4, NOTE_D4,
        NOTE_CS4, NOTE_CS4, NOTE_D4, NOTE_E4, NOTE_D4, NOTE_CS4, NOTE_CS4,
        NOTE_D4, NOTE_D4, NOTE_E4, NOTE_CS4, NOTE_D4, NOTE_E4, NOTE_FS4, NOTE_E4,
        NOTE_CS4, NOTE_D4, NOTE_E4, NOTE_FS4, NOTE_E4, NOTE_D4, NOTE_CS4, NOTE_D4, NOTE_D3
    };

    const int noteDurations[] = {
        250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 375, 125, 500,
        250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 250, 375, 125, 500,
        250, 250, 250, 250, 250, 125, 125, 250, 250, 250, 125, 125, 250, 250, 250, 250, 250
    };
    
    int musicPin = _pwm_pin_az;
    const int soundPWM = 128;
    
    // Play melody
    digitalWrite(_ccw_pin_az, 1);
    Serial.println("Playing Ode to Joy on motors...");
    
    for (int noteIndex = 0; noteIndex < sizeof(melody)/sizeof(melody[0]); noteIndex++) {
        analogWriteFrequency(musicPin, melody[noteIndex]);   
        analogWrite(musicPin, soundPWM);
        delay(noteDurations[noteIndex]);
        analogWrite(musicPin, 255);
        delay(30);
    }
    
    // Reset and restore state
    analogWriteFrequency(musicPin, FREQ);
    setPWM(musicPin, 255);
    activateCalMode(previousCalMode);
    
    _logger.info("Ode to Joy finished");
}