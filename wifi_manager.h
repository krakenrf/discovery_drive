/*
 * Firmware for the discovery-drive satellite dish rotator.
 * WiFi manager - Manages WiFi connections. We use ESP-IDF instead of 
 * the Arduino library as the Arduino library appears to be unstable 
 * with mesh networks. When the arduino method is used, there are 
 * constant disconnections on mesh networks.
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

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

// Arduino/System includes
#include <Arduino.h>
#include <Preferences.h>
#include <ESPmDNS.h>

// ESP-IDF includes
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi_types.h"

// Custom includes
#include "logger.h"

class WiFiManager {
public:
    // Constructor
    WiFiManager(Preferences& prefs, Logger& logger);
    
    // Core functionality
    void begin();
    void connectToWiFi();
    void startAPMode();
    
    // Status and information methods
    String getCurrentBSSID();
    String getCurrentWiFiChannel();
    int32_t getRSSI();
    void printCurrentBSSID();
    int getSignalStrengthLevel(int32_t rssi);
    
    // Public state variables
    String ip_addr = "0.0.0.0";
    bool wifiConnected = false;
    
private:
    // Core references
    Preferences& _preferences;
    Logger& _logger;
    
    // WiFi credentials
    String wifi_ssid;
    String wifi_password;
    
    // Access Point configuration
    const char* _ap_ssid = "discoverydish_HOTSPOT";
    const char* _ap_password = "discoverydish";
    
    // Network configuration
    String _hostname = "discoverydish";
    
    // ESP-IDF network interface
    esp_netif_t* sta_netif = NULL;
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    
    // Static members
    static WiFiManager* _instance;
    static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
};

#endif