/*
 * Firmware for the discovery-drive satellite dish rotator.
 * EMC test mode - Minimal boot mode for EMC compliance testing.
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

#include "emc_test.h"

// The EMC test page is embedded in the firmware (not LittleFS) so that EMC
// mode always has a working UI, even on devices whose filesystem was never
// updated after a firmware upgrade.
static const char EMC_PAGE[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Discovery Drive - EMC Test Mode</title>
<style>
body{font-family:Arial,Helvetica,sans-serif;background:#1a1a2e;color:#eee;margin:0;padding:20px;}
.box{max-width:560px;margin:0 auto;background:#16213e;border-radius:8px;padding:24px;}
h1{margin-top:0;font-size:1.5em;color:#f9a826;}
p{line-height:1.5;}
.warn{background:#4a1f1f;border:1px solid #a33;border-radius:6px;padding:10px 14px;margin:14px 0;}
.msg{display:none;background:#1f3a4a;border:1px solid #3a7ca5;border-radius:6px;padding:10px 14px;margin:14px 0;white-space:pre-line;}
label{display:block;margin:16px 0 6px;}
select,input[type=number]{font-size:1em;padding:6px;border-radius:4px;background:#0f3460;color:#eee;border:1px solid #3a7ca5;}
button{display:block;width:100%;font-size:1em;padding:12px;margin:12px 0;border:none;border-radius:6px;cursor:pointer;color:#fff;}
.start{background:#c0392b;}
.start:hover{background:#e74c3c;}
.other{background:#0f3460;}
.other:hover{background:#1a4a80;}
</style>
</head>
<body>
<div class="box">
<h1>EMC Test Mode</h1>
<div class="warn"><strong>Motors are disabled.</strong> This mode is for EMC compliance testing only. Starting the CW transmission reboots the device into a WiFi-off carrier transmit state.</div>
<div class="msg" id="msg"></div>
<label for="channel">CW channel (2.4&nbsp;GHz WiFi)</label>
<select id="channel"></select>
<label for="atten">TX power attenuation (0&ndash;20&nbsp;dB below maximum, 0.25&nbsp;dB steps)</label>
<input type="number" id="atten" min="0" max="20" step="0.25" value="0">
<button class="start" onclick="startCW()">Start CW Transmission</button>
<p>The carrier transmits continuously until the device is power cycled or reset. After a power cycle the device boots back into this page.</p>
<button class="other" onclick="clearFlag()">Clear EMC Test Mode Flag</button>
<button class="other" onclick="reboot()">Reboot</button>
<p>Clearing the flag does not reboot by itself &mdash; press Reboot afterwards to return to the regular interface.</p>
</div>
<script>
var sel=document.getElementById('channel');
for(var c=1;c<=13;c++){var o=document.createElement('option');o.value=c;o.text='Channel '+c+' ('+(2407+5*c)+' MHz)';sel.appendChild(o);}
sel.value='6';
fetch('/emcStatus').then(function(r){return r.json();}).then(function(d){if(d.channel>=1&&d.channel<=13)sel.value=d.channel;if(typeof d.attenDb=='number')document.getElementById('atten').value=d.attenDb;}).catch(function(){});
function msg(t){var m=document.getElementById('msg');m.textContent=t;m.style.display='block';}
function post(url,ok){fetch(url,{method:'POST'}).then(function(r){if(!r.ok)throw new Error(r.status);return r.text();}).then(ok).catch(function(e){msg('Request failed: '+e);});}
function startCW(){var c=sel.value;
var a=parseFloat(document.getElementById('atten').value);
if(isNaN(a)||a<0||a>20){alert('Attenuation must be between 0 and 20 dB. The PHY ignores larger values (transmits at full power).');return;}
if(!confirm('Start CW transmission on channel '+c+' ('+(2407+5*c)+' MHz) at '+a+' dB attenuation?\n\nThe device will reboot and transmit a continuous carrier with WiFi OFF. This page will become unreachable.\n\nPower cycle the device to stop the carrier and return to this page.'))return;
post('/startCW?channel='+c+'&atten='+a,function(){msg('Rebooting into CW transmit on channel '+c+' ('+(2407+5*c)+' MHz), '+a+' dB attenuation.\nWiFi is now off. Power cycle the device to stop the carrier and return to this page.');});}
function clearFlag(){if(!confirm('Clear the EMC test mode flag?\n\nAfter the next reboot the device will return to the regular interface.'))return;
post('/clearEmcMode',function(){msg('EMC test mode flag cleared. Press Reboot to return to the regular interface.');});}
function reboot(){if(!confirm('Reboot the device?'))return;
post('/restart',function(){msg('Rebooting...');setTimeout(function(){location.reload();},8000);});}
</script>
</body>
</html>
)rawliteral";

EmcTestMode::EmcTestMode(Preferences& prefs, WiFiManager& wifiManager, Logger& logger)
    : _preferences(prefs), _wifiManager(wifiManager), _logger(logger) {
}

bool EmcTestMode::isEnabled() {
    return _preferences.getBool("emc_mode", false);
}

void EmcTestMode::run() {
    parkMotorPins();

    if (_preferences.getBool("emc_cw", false)) {
        // One-shot flag: clearing it before transmitting guarantees any
        // reboot or power cycle ends the carrier and returns to the EMC page.
        _preferences.putBool("emc_cw", false);
        runCwTransmitForever();
    }

    runWebUiForever();
}

// Motor driver pins are never initialized in EMC mode. PWM is inverted
// (HIGH = stopped) — drive the pins to the stopped state explicitly so they
// don't float. Pin numbers match motor_controller.h.
void EmcTestMode::parkMotorPins() {
    const int pwmPins[] = {35, 40};
    const int dirPins[] = {36, 41};
    for (int pin : pwmPins) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
    }
    for (int pin : dirPins) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
    }
}

// CW carrier transmit state. WiFi is intentionally never initialized on this
// boot path — the RF test API takes exclusive ownership of the PHY, exactly
// like Espressif's cert_test example.
void EmcTestMode::runCwTransmitForever() {
    int chan = _preferences.getInt("emc_chan", 6);
    chan = constrain(chan, 1, 13);
    const int freqMhz = 2407 + 5 * chan;

    // Attenuation stored in the API's native unit of 0.25 dB steps.
    // Capped at 80 (20 dB): larger values overflow the PHY's internal
    // signed 8-bit power math and are silently treated as 0 (full power).
    int backoff = constrain(_preferences.getInt("emc_backoff", 0), 0, 80);
    const float attenDb = backoff * 0.25f;

    Serial.printf("EMC test mode: starting CW carrier on channel %d (%d MHz), %.2f dB attenuation\r\n", chan, freqMhz, attenDb);

    esp_wifi_power_domain_on();
    esp_phy_rftest_config(1);
    esp_phy_rftest_init();
    esp_phy_wifi_tx_tone(1, (uint32_t)chan, (uint32_t)backoff);

    for (;;) {
        Serial.printf("EMC CW active on channel %d (%d MHz), %.2f dB attenuation. Reboot or power cycle to stop.\r\n", chan, freqMhz, attenDb);
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void EmcTestMode::runWebUiForever() {
    _logger.warn("EMC TEST MODE ACTIVE - motors disabled, serving EMC test page");

    safeCopy(_loginUser, _preferences.getString("loginUser", "").c_str(), sizeof(_loginUser));
    safeCopy(_loginPassword, _preferences.getString("loginPassword", "").c_str(), sizeof(_loginPassword));

    // The EMC page must always be reachable — override a persistent WiFi disable
    _preferences.putBool("wifiDisabled", false);
    _wifiManager.begin();

    _server = new WebServer(_preferences.getInt("http_port", 80));
    setupRoutes();
    _server->begin();
    _logger.info("EMC test HTTP server started");

    xTaskCreatePinnedToCore(
        WebTask
        ,  "EMC Web Server"
        ,  16384
        ,  this
        ,  1  // Priority (lower than LWIP tasks)
        ,  NULL
        ,  1   // Core to run on
    );

    vTaskDelete(NULL);  // Arduino loopTask has nothing left to do
}

bool EmcTestMode::checkAuth() {
    if (_loginUser[0] != '\0' && _loginPassword[0] != '\0') {
        if (!_server->authenticate(_loginUser, _loginPassword)) {
            _server->requestAuthentication();
            return false;
        }
    }
    return true;
}

void EmcTestMode::setupRoutes() {
    _server->on("/", HTTP_GET, [this]() {
        if (!checkAuth()) return;
        _server->send_P(200, "text/html", EMC_PAGE);
    });

    _server->on("/emcStatus", HTTP_GET, [this]() {
        if (!checkAuth()) return;
        _server->send(200, "application/json",
                      "{\"channel\":" + String(_preferences.getInt("emc_chan", 6)) +
                      ",\"attenDb\":" + String(_preferences.getInt("emc_backoff", 0) * 0.25f, 2) + "}");
    });

    _server->on("/startCW", HTTP_POST, [this]() {
        if (!checkAuth()) return;
        int chan = _server->hasArg("channel") ? _server->arg("channel").toInt() : 0;
        if (chan < 1 || chan > 13) {
            _server->send(400, "text/plain", "Invalid channel (1-13)");
            return;
        }
        // Max 20 dB: the PHY combines the attenuation into a signed 8-bit
        // value internally — larger settings overflow and transmit full power
        float attenDb = _server->hasArg("atten") ? _server->arg("atten").toFloat() : 0.0f;
        if (attenDb < 0.0f || attenDb > 20.0f) {
            _server->send(400, "text/plain", "Invalid attenuation (0-20 dB)");
            return;
        }
        _preferences.putInt("emc_chan", chan);
        _preferences.putInt("emc_backoff", (int)roundf(attenDb * 4.0f));
        _preferences.putBool("emc_cw", true);
        _logger.warn("EMC CW transmit requested on channel " + String(chan) + " at " + String(attenDb, 2) + " dB attenuation - rebooting");
        _server->send(200, "text/plain", "OK");
        delay(1000);
        ESP.restart();
    });

    _server->on("/clearEmcMode", HTTP_POST, [this]() {
        if (!checkAuth()) return;
        _preferences.putBool("emc_mode", false);
        _preferences.putBool("emc_cw", false);
        _logger.info("EMC test mode flag cleared - next reboot returns to normal firmware");
        _server->send(200, "text/plain", "OK");
    });

    _server->on("/restart", HTTP_POST, [this]() {
        if (!checkAuth()) return;
        _server->send(200, "text/plain", "OK");
        delay(1000);
        ESP.restart();
    });

    _server->onNotFound([this]() {
        _server->send(404, "text/plain", "Not found (EMC test mode)");
    });
}

void EmcTestMode::WebTask(void* param) {
    EmcTestMode* self = static_cast<EmcTestMode*>(param);
    TickType_t xLastWakeTime = xTaskGetTickCount();
    const TickType_t xFrequency = 20 / portTICK_PERIOD_MS;
    for (;;) {
        self->_server->handleClient();
        vTaskDelayUntil(&xLastWakeTime, xFrequency);
    }
}
