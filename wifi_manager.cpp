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
 
#include "wifi_manager.h"

// Static member initialization
WiFiManager* WiFiManager::_instance = nullptr;

// Constructor
WiFiManager::WiFiManager(Preferences& prefs, Logger& logger) : _preferences(prefs), _logger(logger) {
    _instance = this;
}

// Core functionality methods
void WiFiManager::begin() {
    safeCopy(_hostname, _preferences.getString("hostname", "discoverydrive").c_str(), sizeof(_hostname));
    _httpPort = _preferences.getInt("http_port", 80);

    if (_preferences.getBool("wifiDisabled", false)) {
        wifiDisabled = true;
        _logger.info("WiFi permanently disabled by user preference");
        return;
    }

    connectToWiFi();
}

void WiFiManager::connectToWiFi() {
    safeCopy(wifi_ssid, _preferences.getString("wifi_ssid", "").c_str(), sizeof(wifi_ssid));
    safeCopy(wifi_password, _preferences.getString("wifi_password", "").c_str(), sizeof(wifi_password));
    _wifiChannelLock = _preferences.getInt("wifi_chan_lock", 0);
    if (_wifiChannelLock < 0 || _wifiChannelLock > 13) _wifiChannelLock = 0;

    // Initialize ESP-IDF networking stack
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    sta_netif = esp_netif_create_default_wifi_sta();
    
    // Configure WiFi with advanced settings
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    cfg.nvs_enable = true;
    cfg.ampdu_rx_enable = true;
    cfg.rx_ba_win = 16;
    
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &WiFiManager::wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &WiFiManager::wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    if (wifi_ssid[0] != '\0' && wifi_password[0] != '\0') {
        _logger.info("Connecting to Wi-Fi...");

        // Configure WiFi station mode
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
        
        // Setup WiFi configuration
        wifi_config_t wifi_config;
        memset(&wifi_config, 0, sizeof(wifi_config_t));
        
        // Authentication and security settings
        wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
        wifi_config.sta.pmf_cfg.capable = true;
        wifi_config.sta.pmf_cfg.required = false;
        
        // Network credentials
        strcpy((char*)wifi_config.sta.ssid, wifi_ssid);
        strcpy((char*)wifi_config.sta.password, wifi_password);
        
        // Roaming and scanning configuration
        wifi_config.sta.bssid_set = 0;
        wifi_config.sta.listen_interval = 3;
        wifi_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;

        if (_wifiChannelLock >= 1 && _wifiChannelLock <= 13) {
            // Channel lock (CE/EMC testing): prefer the locked channel and
            // disable 802.11k/v/r roaming so the radio stays on it
            wifi_config.sta.channel = _wifiChannelLock;
            wifi_config.sta.scan_method = WIFI_FAST_SCAN;
            wifi_config.sta.rm_enabled = 0;
            wifi_config.sta.btm_enabled = 0;
            wifi_config.sta.ft_enabled = 0;
            _logger.info("WiFi channel locked to " + String(_wifiChannelLock) + " (roaming disabled)");
        } else {
            wifi_config.sta.rm_enabled = 1;    // 802.11k
            wifi_config.sta.btm_enabled = 1;   // 802.11v
            wifi_config.sta.ft_enabled = 1;    // 802.11r
            wifi_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
        }

        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());

        // Increase beacon timeout for mesh network compatibility (default 6s)
        // Google Mesh/Nest WiFi aggressively steers clients between nodes,
        // which can cause brief beacon gaps that trigger reason-200 disconnects.
        // 10s is a compromise: short enough to detect a dead node reasonably fast,
        // long enough to tolerate normal mesh steering gaps.
        ESP_ERROR_CHECK(esp_wifi_set_inactive_time(WIFI_IF_STA, 10));

        // Enable proactive roaming: ESP-IDF fires WIFI_EVENT_STA_BSS_RSSI_LOW
        // when signal drops below this threshold, letting us scan for a better AP
        // before the current one becomes unreachable
        if (_wifiChannelLock == 0) {
            esp_wifi_set_rssi_threshold(ROAMING_RSSI_THRESHOLD);
        }

        // Optimize WiFi performance
        ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20));
        ESP_ERROR_CHECK(esp_wifi_set_max_tx_power(84));
    } else {
        _logger.info("No Wi-Fi credentials found, starting AP mode");
        startAPMode();
    }
}

void WiFiManager::startAPMode() {
    _logger.info("Starting Access Point...");
    
    esp_netif_t* ap_netif = esp_netif_create_default_wifi_ap();
    
    // Configure access point
    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config_t));
    
    strcpy((char*)wifi_config.ap.ssid, _ap_ssid);
    strcpy((char*)wifi_config.ap.password, _ap_password);
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.beacon_interval = 100;

    if (_wifiChannelLock >= 1 && _wifiChannelLock <= 13) {
        // Channel lock (CE/EMC testing) — in AP mode the ESP32 owns the channel
        wifi_config.ap.channel = _wifiChannelLock;
        _logger.info("AP channel locked to " + String(_wifiChannelLock));
    }
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    // Get and store IP address
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(ap_netif, &ip_info);
    safeCopy(ip_addr, IPAddress(ip_info.ip.addr).toString().c_str(), sizeof(ip_addr));

    _logger.info("AP IP address: " + String(ip_addr));
}

// Status and information methods
const char* WiFiManager::getCurrentBSSID() {
    static char bssid_str[18];
    wifi_ap_record_t ap_info;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);

    if (err == ESP_OK) {
        sprintf(bssid_str, "%02X:%02X:%02X:%02X:%02X:%02X",
                ap_info.bssid[0], ap_info.bssid[1], ap_info.bssid[2],
                ap_info.bssid[3], ap_info.bssid[4], ap_info.bssid[5]);
        return bssid_str;
    }
    return "Not connected";
}

const char* WiFiManager::getCurrentWiFiChannel() {
    static char channel_str[4];
    wifi_ap_record_t ap_info;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);

    if (err == ESP_OK) {
        snprintf(channel_str, sizeof(channel_str), "%d", ap_info.primary);
        return channel_str;
    }
    return "N/A";
}

int32_t WiFiManager::getRSSI() {
    // Check if WiFi is in station mode
    wifi_mode_t mode;
    if (esp_wifi_get_mode(&mode) != ESP_OK) {
        return 0;
    }
    
    if (mode != WIFI_MODE_STA && mode != WIFI_MODE_APSTA) {
        return 0;
    }
    
    // Get AP info including RSSI
    wifi_ap_record_t ap_info;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
    
    return (err == ESP_OK) ? ap_info.rssi : 0;
}

void WiFiManager::printCurrentBSSID() {
    wifi_ap_record_t ap_info;
    esp_err_t err = esp_wifi_sta_get_ap_info(&ap_info);
    
    if (err == ESP_OK) {
        char bssid_str[18];
        sprintf(bssid_str, "%02X:%02X:%02X:%02X:%02X:%02X", 
                ap_info.bssid[0], ap_info.bssid[1], ap_info.bssid[2], 
                ap_info.bssid[3], ap_info.bssid[4], ap_info.bssid[5]);
        
        _logger.info("Connected to BSSID: " + String(bssid_str) + 
                    ", RSSI: " + String(ap_info.rssi) + " dBm" +
                    ", Channel: " + String(ap_info.primary));
    } else {
        _logger.info("Failed to get AP info");
    }
}

int WiFiManager::getSignalStrengthLevel(int32_t rssi) {
    if (rssi >= -50) return 4;      // Full signal (4 bars)
    if (rssi >= -60) return 3;      // 3 bars
    if (rssi >= -70) return 2;      // 2 bars
    if (rssi >= -80) return 1;      // 1 bar
    return 0;                       // No signal
}

const char* WiFiManager::getHostname() {
    return _hostname;
}

// WiFi disable/enable methods
void WiFiManager::disableWiFi(bool persistent) {
    wifiDisabled = true;
    if (persistent) {
        _preferences.putBool("wifiDisabled", true);
    }
    esp_wifi_disconnect();
    esp_wifi_stop();
    wifiConnected = false;
    _logger.info("WiFi disabled" + String(persistent ? " (persistent)" : " (until reboot)"));
}

void WiFiManager::enableWiFi(bool persistent) {
    if (persistent) {
        _preferences.putBool("wifiDisabled", false);
    }
    wifiDisabled = false;
    if (sta_netif == NULL) {
        // WiFi was never initialized (persistent disable at boot) — full init
        connectToWiFi();
    } else {
        // WiFi was previously running — just restart
        esp_wifi_start();
        esp_wifi_connect();
    }
    _logger.info("WiFi enabled" + String(persistent ? " (persistent)" : " (until reboot)"));
}

// Static event handler
void WiFiManager::wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    WiFiManager* inst = _instance;  // Local copy
    if (inst == nullptr) return;

    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START: {
                inst->_logger.info("WiFi station mode started");
                esp_err_t result = esp_wifi_connect();
                if (result != ESP_OK && result != ESP_ERR_WIFI_CONN) {
                    inst->_logger.error("WiFi connect failed: " + String(result));
                }
                break;
            }
            
            case WIFI_EVENT_STA_CONNECTED: {
                wifi_event_sta_connected_t* event = (wifi_event_sta_connected_t*)event_data;
                char bssid_str[18];
                sprintf(bssid_str, "%02x:%02x:%02x:%02x:%02x:%02x", 
                        event->bssid[0], event->bssid[1], event->bssid[2], 
                        event->bssid[3], event->bssid[4], event->bssid[5]);
                
                inst->_logger.info("Connected to AP BSSID: " + String(bssid_str) + 
                                       ", Channel: " + String(event->channel));
                inst->wifiConnected = true;
                break;
            }
            
            case WIFI_EVENT_STA_BSS_RSSI_LOW: {
                if (inst->_wifiChannelLock != 0) break;  // no roaming while channel is locked
                unsigned long now = millis();
                if (now - inst->_lastRoamingAttemptMs > ROAMING_COOLDOWN_MS) {
                    inst->_lastRoamingAttemptMs = now;
                    inst->_logger.warn("Low RSSI (below " + String(ROAMING_RSSI_THRESHOLD) +
                                       " dBm) — disconnecting to scan for better AP");
                    esp_wifi_disconnect();  // triggers DISCONNECTED event which reconnects to strongest AP
                } else {
                    inst->_logger.debug("Low RSSI detected but roaming cooldown active");
                }
                break;
            }

            case WIFI_EVENT_STA_DISCONNECTED: {
                wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*)event_data;
                uint8_t reason = event->reason;
                inst->_logger.error("WiFi disconnected. Reason: " + String(reason));

                // Don't reconnect if WiFi was intentionally disabled
                if (inst->wifiDisabled.load()) return;

                // Roaming-related reasons — the ESP-IDF 802.11k/v/r stack is
                // transitioning to a new AP.  Don't mark WiFi as down; just
                // trigger reconnect as a safety net.
                bool isRoaming = (reason == WIFI_REASON_ASSOC_LEAVE  // 8
                               || reason == 200);                          // ROAMING

                if (isRoaming) {
                    inst->_logger.info("Roaming event (reason " + String(reason) + ") — reconnecting to best AP");
                } else {
                    inst->wifiConnected = false;
                }

                // Attempt reconnection (safe even during roaming)
                esp_err_t result = esp_wifi_connect();
                if (result != ESP_OK && result != ESP_ERR_WIFI_CONN) {
                    inst->_logger.error("WiFi reconnect failed: " + String(result));
                }
                break;
            }
        }
    } 
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        inst->_logger.info("Got IP: " + IPAddress(event->ip_info.ip.addr).toString());

        safeCopy(inst->ip_addr, IPAddress(event->ip_info.ip.addr).toString().c_str(), sizeof(inst->ip_addr));
        inst->wifiConnected = true;

        // (Re)start mDNS every time we get an IP
        MDNS.end();
        if (MDNS.begin(inst->_hostname)) {
            inst->_logger.info("mDNS responder started: http://" + String(inst->_hostname) + ".local");
            MDNS.addService("http", "tcp", inst->_httpPort);
        } else {
            inst->_logger.error("mDNS failed to start");
        }
    }
}