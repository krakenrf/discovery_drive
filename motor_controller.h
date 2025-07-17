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
 
#ifndef MOTOR_CONTROLLER_H
#define MOTOR_CONTROLLER_H

// System includes
#include <Arduino.h>
#include <atomic>
#include <Wire.h>
#include <Preferences.h>

// Custom includes
#include "ina219_manager.h"
#include "logger.h"

class MotorSensorController {
public:
    // Constructor
    MotorSensorController(Preferences& prefs, INA219Manager& ina219Manager, Logger& logger);

    // Core control methods
    void begin();
    void runControlLoop();
    void runSafetyLoop();

    // Setpoint and angle access methods
    float getSetPointAz();
    float getSetPointEl();
    void setSetPointAz(float setpoint_az);
    void setSetPointEl(float setpoint_el);
    void setErrorAz(float value);
    void setErrorEl(float value);
    
    double getErrorAz();
    double getErrorEl();
    float getCorrectedAngleAz();
    float getCorrectedAngleEl();
    void setCorrectedAngleAz(float value);
    void setCorrectedAngleEl(float value);
    
    float getElStartAngle();
    void setElStartAngle(float value);
    int getMaxPowerBeforeFault();
    void setMaxPowerBeforeFault(int value);

    // Configuration parameter getters
    int getPEl() const { return P_el; }
    int getPAz() const { return P_az; }
    int getMinElSpeed() const { return MIN_EL_SPEED; }
    int getMinAzSpeed() const { return MIN_AZ_SPEED; }
    float getMinAzTolerance() const { return _MIN_AZ_TOLERANCE; }
    float getMinElTolerance() const { return _MIN_EL_TOLERANCE; }
    
    // Configuration parameter setters
    void setPEl(int value);
    void setPAz(int value);
    void setMinElSpeed(int value);
    void setMinAzSpeed(int value);
    void setMinAzTolerance(float value);
    void setMinElTolerance(float value);

    // Calibration and special functions
    void activateCalMode(bool on);
    void calMoveMotor(const String& runTimeStr, const String& axis);
    void calibrate_elevation();
    void playOdeToJoy();

    // Utility methods
    int convertPercentageToSpeed(float percentage);
    int convertSpeedToPercentage(float speed);
    void handleCalibrationMode();
    void handleOscillationDetection();
    void updateI2CErrorCounter(int i2c_addr);
    void resetI2CErrorCounter(int i2c_addr);

    // Motor control state
    std::atomic<bool> setPointState_az = false;
    std::atomic<bool> setPointState_el = false;
    std::atomic<bool> _isAzMotorLatched = false;
    std::atomic<bool> _isElMotorLatched = false;
    
    // Operating modes
    std::atomic<bool> calMode = false;
    std::atomic<bool> singleMotorMode = false;
    std::atomic<int> needs_unwind = 0;
    
    // Fault and error flags
    std::atomic<bool> global_fault = false;
    std::atomic<bool> outOfBoundsFault = false;
    std::atomic<bool> overSpinFault = false;
    std::atomic<bool> magnetFault = false;
    std::atomic<bool> badAngleFlag = false;
    std::atomic<bool> overPowerFault = false;
    std::atomic<bool> lowVoltageFault = false;
    std::atomic<bool> i2cErrorFlag_az = false;
    std::atomic<bool> i2cErrorFlag_el = false;
    
    // New convergence safety fault flags
    std::atomic<bool> errorDivergenceFault = false;
    
    // Motor speed configuration
    std::atomic<int> MIN_EL_SPEED = 50;
    std::atomic<int> MIN_AZ_SPEED = 100;
    std::atomic<int> max_dual_motor_az_speed = MAX_AZ_SPEED;
    std::atomic<int> max_dual_motor_el_speed = MAX_EL_SPEED;
    std::atomic<int> max_single_motor_az_speed = 0;
    std::atomic<int> max_single_motor_el_speed = 0;

private:
    // Dependencies
    Preferences& _preferences;
    INA219Manager& ina219Manager;
    Logger& _logger;

    // Hardware configuration constants
    static constexpr int _el_hall_i2c_addr = 0x36;  // AS5600
    static constexpr int _az_hall_i2c_addr = 0x40;  // AS5600L
    
    static constexpr int _pwm_pin_az = 35;          // PWM speed control (BLUE)
    static constexpr int _ccw_pin_az = 36;          // Direction control (WHITE)
    static constexpr int _pwm_pin_el = 40;
    static constexpr int _ccw_pin_el = 41;
    
    static constexpr int FREQ = 20000;              // 20 kHz PWM frequency
    static constexpr int MAX_AZ_SPEED = 0;
    static constexpr int MAX_EL_SPEED = 0;
    
    static constexpr int _numAvg = 10;              // Sensor averaging samples
    static constexpr uint8_t MAX_CONSECUTIVE_ERRORS = 5;

    // Error convergence safety constants
    static constexpr int ERROR_HISTORY_SIZE = 20;              // Number of error samples to track
    static constexpr unsigned long ERROR_SAMPLE_INTERVAL = 250; // ms between samples (matches control loop)
    static constexpr float DIVERGENCE_THRESHOLD = 1.1f;        // Factor by which error must increase to trigger fault
    static constexpr float STALL_THRESHOLD = 0.01f;            // Minimum change rate (degrees/sec) to avoid stall fault
    static constexpr unsigned long CONVERGENCE_TIMEOUT = 3000; // ms to wait before checking for stalls

    // Control parameters (configurable)
    int P_el = 100;
    int P_az = 5;
    float _MIN_AZ_TOLERANCE = 1.5;
    float _MIN_EL_TOLERANCE = 0.1;
    std::atomic<int> _maxPowerBeforeFault = 10;

    // Setpoints and errors (thread-safe)
    volatile float _setpoint_az = 0;
    volatile float _setpoint_el = 0;
    volatile double _error_az = 0;
    volatile double _error_el = 0;
    volatile float _correctedAngle_az = 0;
    volatile float _correctedAngle_el = 0;
    
    // Update flags
    std::atomic<bool> _setPointAzUpdated = false;
    std::atomic<bool> _setPointElUpdated = false;
    std::atomic<bool> _az_priority = true;
    
    // Motor control state
    int _current_speed_az = 0;
    int _current_speed_el = 0;
    double _last_error_az = 0;
    double _last_error_el = 0;
    double _prev_error_az = 0.0;
    double _prev_error_el = 0.0;
    int _maxAdjustedSpeed_az = 0;
    int _maxAdjustedSpeed_el = 0;
    bool _jitterAzMotors = false;
    bool _jitterElMotors = false;
    
    // Angle and positioning state
    float _az_startAngle = 0;
    float _el_startAngle = 0;
    int _prev_needs_unwind = 0;
    int _quadrantNumber_az = 0;
    int _previousquadrantNumber_az = 0;
    
    // Oscillation detection
    unsigned long _oscillationTimerStart = 0;
    int _oscillationCount = 0;
    bool _oscillationTimerActive = false;

    // I2C error tracking
    uint8_t _consecutivei2cErrors_az = 0;
    uint8_t _consecutivei2cErrors_el = 0;

    // Calibration state
    int _calRunTime = 0;
    String _calAxis = "";
    int _calState = 0;
    unsigned long _calMoveStartTime = 0;

    // Error convergence safety tracking
    struct ErrorTracker {
        float errorHistory[ERROR_HISTORY_SIZE];
        unsigned long timestamps[ERROR_HISTORY_SIZE];
        int currentIndex;
        int sampleCount;
        unsigned long lastSampleTime;
        unsigned long setpointChangeTime;
        bool motorShouldBeActive;
        
        ErrorTracker() : currentIndex(0), sampleCount(0), lastSampleTime(0), 
                        setpointChangeTime(0), motorShouldBeActive(false) {
            memset(errorHistory, 0, sizeof(errorHistory));
            memset(timestamps, 0, sizeof(timestamps));
        }
    };
    
    ErrorTracker _azErrorTracker;
    ErrorTracker _elErrorTracker;

    // Thread synchronization
    SemaphoreHandle_t _setPointMutex = NULL;
    SemaphoreHandle_t _getAngleMutex = NULL;
    SemaphoreHandle_t _correctedAngleMutex = NULL;
    SemaphoreHandle_t _errorMutex = NULL;
    SemaphoreHandle_t _el_startAngleMutex = NULL;

    // Motor control methods
    void actuate_motor_az(int min_speed);
    void actuate_motor_el(int min_speed);
    void setPWM(int pin, int pwm_value);
    void updateMotorControl(float current_setpoint_az, float current_setpoint_el, 
                           bool setPointAzUpdated, bool setPointElUpdated);
    void updateMotorPriority(bool setPointAzUpdated, bool setPointElUpdated);
    
    // Angle calculation methods
    void angle_shortest_error_az(float target_angle, float current_angle);
    void angle_error_el(float target_angle, float current_angle);
    float correctAngle(float startAngle, float inputAngle);
    void calcIfNeedsUnwind(float correctedAngle_az);
    
    // Sensor interface methods
    float getAvgAngle(int i2c_addr);
    float ReadRawAngle(int i2c_addr);
    int checkMagnetPresence(int i2c_addr);
    float calculateAngleMeanWithDiscard(float* array, int size);
    
    // Error convergence safety methods
    void updateErrorTracking();
    void checkErrorConvergence();
    bool isErrorDiverging(const ErrorTracker& tracker, float tolerance);
    bool isConvergenceStalled(const ErrorTracker& tracker, float tolerance);
    void resetErrorTracker(ErrorTracker& tracker);
    float calculateErrorChangeRate(const ErrorTracker& tracker);
    void checkStall();
    
    // Utility methods
    void slowPrint(const String& message, int messageID);
};

#endif