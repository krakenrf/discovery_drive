/*
 * Firmware for the discovery-drive satellite dish rotator.
 * Stellarium Poller - Poll the stellarium web data interface and send
 * control information to the motors.
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

#ifndef STELLARIUM_POLLER_H
#define STELLARIUM_POLLER_H

// System includes
#include <Preferences.h>
#include <HTTPClient.h>
#include <atomic>

// Custom includes
#include "motor_controller.h"
#include "logger.h"

class StellariumPoller {
public:
    // Constructor
    StellariumPoller(Preferences& prefs, MotorSensorController& motorController, Logger& logger);

    // Core functionality
    void begin();
    void runStellariumLoop(bool serialActive, String rotctl_client_ip, bool wifiConnected);

    // Connection status getters and setters
    bool getStellariumConnActive();
    void setStellariumConnActive(bool on);

    // Stellarium mode getters and setters
    bool getStellariumOn();
    void setStellariumOn(bool on);

private:
    // Dependencies
    Preferences& _preferences;
    MotorSensorController& _motorSensorCtrl;
    Logger& _logger;

    // State variables (thread-safe)
    std::atomic<bool> _stellariumOn = false;
    std::atomic<bool> _stellariumConnActive = false;

    // Core functionality helpers
    bool shouldPollStellarium(bool serialActive, String rotctl_client_ip);
    bool pollStellariumData();
    bool processApiResponse(const String& payload);

    // Utility methods
    String getValue(String data, String start, String end);
    double parseDMS(String dms);
};

#endif // STELLARIUM_POLLER_H