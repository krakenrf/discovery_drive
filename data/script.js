/*
 * Firmware for the discovery-drive satellite dish rotator.
 * script.js - Javascript for the web UI.
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

// Simple pause state for log scrolling
var logScrollPaused = false;
var logLines = []; // Store log lines in browser memory
var MAX_LOG_LINES = 100000;

setInterval(function() {
  var xhr = new XMLHttpRequest();
  xhr.open("GET", "/variable", true);
  xhr.onreadystatechange = function() {
    if (xhr.readyState == 4 && xhr.status == 200) {
      var data = JSON.parse(xhr.responseText);
      document.getElementById("correctedAngle_el").innerHTML = data.correctedAngle_el;
      document.getElementById("correctedAngle_az").innerHTML = data.correctedAngle_az;
      document.getElementById("setpoint_az").innerHTML = data.setpoint_az;
      document.getElementById("setpoint_el").innerHTML = data.setpoint_el;
      document.getElementById("setPointState_az").innerHTML = data.setPointState_az;
      document.getElementById("setPointState_el").innerHTML = data.setPointState_el;
      document.getElementById("error_az").innerHTML = data.error_az;
      document.getElementById("error_el").innerHTML = data.error_el;
      document.getElementById("el_startAngle").innerHTML = data.el_startAngle;
      document.getElementById("needs_unwind").innerHTML = data.needs_unwind;
      document.getElementById("calMode").innerHTML = data.calMode;
      document.getElementById("i2cErrorFlag_az").innerHTML = data.i2cErrorFlag_az;
      document.getElementById("i2cErrorFlag_el").innerHTML = data.i2cErrorFlag_el;
      document.getElementById("badAngleFlag").innerHTML = data.badAngleFlag;
      document.getElementById("magnetFault").innerHTML = data.magnetFault;
      document.getElementById("faultTripped").innerHTML = data.faultTripped;
      document.getElementById("http_port").innerHTML = data.http_port;
      document.getElementById("rotctl_port").innerHTML = data.rotctl_port;
      document.getElementById("maxDualMotorAzSpeed").innerHTML = data.maxDualMotorAzSpeed;
      document.getElementById("maxDualMotorElSpeed").innerHTML = data.maxDualMotorElSpeed;
      document.getElementById("maxSingleMotorAzSpeed").innerHTML = data.maxSingleMotorAzSpeed;
      document.getElementById("maxSingleMotorElSpeed").innerHTML = data.maxSingleMotorElSpeed;
      document.getElementById("wifissid").innerHTML = data.wifissid;
      document.getElementById("loginUser").innerHTML = data.loginUser;
      document.getElementById("passwordStatus").innerHTML = data.passwordStatus;
      document.getElementById("serialActive").innerHTML = data.serialActive;
      document.getElementById("singleMotorModeText").innerHTML = data.singleMotorModeText;
      document.getElementById("toleranceAz").innerHTML = data.toleranceAz;
      document.getElementById("toleranceEl").innerHTML = data.toleranceEl;
      document.getElementById("isAzMotorLatched").innerHTML = data.isAzMotorLatched;
      document.getElementById("isElMotorLatched").innerHTML = data.isElMotorLatched;
      document.getElementById("stellariumPollingOn").innerHTML = data.stellariumPollingOn;
      document.getElementById("stellariumServerIPText").innerHTML = data.stellariumServerIPText;
      document.getElementById("stellariumServerPortText").innerHTML = data.stellariumServerPortText;
      document.getElementById("stellariumConnActive").innerHTML = data.stellariumConnActive;
      document.getElementById("inputVoltage").innerHTML = data.inputVoltage;
      document.getElementById("currentDraw").innerHTML = data.currentDraw;
      document.getElementById("rotatorPowerDraw").innerHTML = data.rotatorPowerDraw;
      document.getElementById('ip_addr').innerHTML = data.ip_addr;
      document.getElementById('rotctl_client_ip').innerHTML = data.rotctl_client_ip;
      document.getElementById('bssid').innerHTML = data.bssid;
      document.getElementById('wifi_channel').innerHTML = data.wifi_channel;

      document.getElementById('rssi').innerHTML = data.rssi;
      var level = data.level;
      document.getElementById('bar1').classList.toggle('active', level >= 1);
      document.getElementById('bar2').classList.toggle('active', level >= 2);
      document.getElementById('bar3').classList.toggle('active', level >= 3);
      document.getElementById('bar4').classList.toggle('active', level >= 4);

      // Wind safety status updates
      document.getElementById("windStowActive").innerHTML = data.windStowActive;
      document.getElementById("windStowReason").innerHTML = data.windStowReason;
      document.getElementById("windSafetyEnabled").innerHTML = data.windSafetyEnabled;
      document.getElementById("windBasedHomeEnabled").innerHTML = data.windBasedHomeEnabled;
      document.getElementById("windSpeedThreshold").innerHTML = data.windSpeedThreshold;
      document.getElementById("windGustThreshold").innerHTML = data.windGustThreshold;
      document.getElementById("emergencyStowActive").innerHTML = data.emergencyStowActive;
      document.getElementById("stowDirection").innerHTML = data.stowDirection;

      // Update wind safety checkboxes
      var windSafetyCheckbox = document.getElementById("windSafetyToggle");
      if (windSafetyCheckbox) {
        windSafetyCheckbox.checked = (data.windSafetyEnabled === "ON");
      }

      var windBasedHomeCheckbox = document.getElementById("windBasedHomeToggle");
      if (windBasedHomeCheckbox) {
        windBasedHomeCheckbox.checked = (data.windBasedHomeEnabled === "ON");
      }

      // Show/hide wind stow alert
      var windStowAlert = document.getElementById("windStowAlert");
      var windStowMessage = document.getElementById("windStowMessage");
      if (data.emergencyStowActive === "YES") {
        windStowMessage.innerHTML = "⚠️ EMERGENCY WIND STOW ACTIVE: " + data.windStowReason;
        windStowAlert.style.display = "block";
      } else {
        windStowAlert.style.display = "none";
      }

      // Handle new log messages with browser-side rolling buffer
      if (data.newLogMessages && data.newLogMessages.trim() !== "") {
        // Split new messages into lines and add them
        var newLines = data.newLogMessages.split('\n');
        for (var i = 0; i < newLines.length; i++) {
          if (newLines[i].trim() !== "") {
            logLines.push(newLines[i]);

            // Keep only the last MAX_LOG_LINES
            if (logLines.length > MAX_LOG_LINES) {
              logLines.shift(); // Remove oldest line
            }
          }
        }
      }

      updateLogDisplay();

      // Update debug level if element exists
      if (document.getElementById("currentDebugLevel")) {
        document.getElementById("currentDebugLevel").innerHTML = data.currentDebugLevel;
        updateDebugLevelDropdown(data.currentDebugLevel);
      }

      // Update serial output disabled status
      if (document.getElementById("serialOutputDisabled")) {
        document.getElementById("serialOutputDisabled").innerHTML = data.serialOutputDisabled ? "True" : "False";
      }

      // Update the checkbox state based on the current setting
      var disableSerialOutputCheckbox = document.getElementById("disableSerialOutput");
      if (disableSerialOutputCheckbox) {
        disableSerialOutputCheckbox.checked = data.serialOutputDisabled;
      }

      document.getElementById("P_el").innerHTML = data.P_el;
      document.getElementById("P_az").innerHTML = data.P_az;
      document.getElementById("MIN_EL_SPEED").innerHTML = data.MIN_EL_SPEED;
      document.getElementById("MIN_AZ_SPEED").innerHTML = data.MIN_AZ_SPEED;
      document.getElementById("MIN_AZ_TOLERANCE").innerHTML = data.MIN_AZ_TOLERANCE;
      document.getElementById("MIN_EL_TOLERANCE").innerHTML = data.MIN_EL_TOLERANCE;
      document.getElementById("MAX_FAULT_POWER").innerHTML = data.MAX_FAULT_POWER;
      document.getElementById("MIN_VOLTAGE_THRESHOLD").innerHTML = data.MIN_VOLTAGE_THRESHOLD;

      var azimuth = data.correctedAngle_az; // Replace with actual AZ data from Arduino
      var elevation = data.correctedAngle_el; // Replace with actual EL data from Arduino
      var setpoint_az = data.setpoint_az; // Replace with actual Setpoint AZ
      var setpoint_el = data.setpoint_el; // Replace with actual Setpoint EL
      
      
      // Weather data updates
      document.getElementById("weatherEnabled").innerHTML = data.weatherEnabled;
      document.getElementById("weatherApiKeyConfigured").innerHTML = data.weatherApiKeyConfigured;
      document.getElementById("weatherLocationConfigured").innerHTML = data.weatherLocationConfigured;
      document.getElementById("weatherLatitude").innerHTML = data.weatherLatitude;
      document.getElementById("weatherLongitude").innerHTML = data.weatherLongitude;
      document.getElementById("weatherDataValid").innerHTML = data.weatherDataValid;
      document.getElementById("weatherLastUpdate").innerHTML = data.weatherLastUpdate;
      
      // Update weather polling checkbox
      var weatherCheckbox = document.getElementById("weatherPolling");
      if (weatherCheckbox) {
        weatherCheckbox.checked = (data.weatherEnabled === "ON");
      }
      
      // Current weather conditions
      document.getElementById("currentWindSpeed").innerHTML = data.currentWindSpeed;
      document.getElementById("currentWindDirection").innerHTML = formatWindDirection(data.currentWindDirection);
      document.getElementById("currentWindGust").innerHTML = data.currentWindGust;
      document.getElementById("currentWeatherTime").innerHTML = formatWeatherTime(data.currentWeatherTime);
      
      // Forecast data
      if (data.forecastWindSpeed && data.forecastWindSpeed.length >= 3) {
        for (var i = 0; i < 3; i++) {
          document.getElementById("forecastTime" + i).innerHTML = formatWeatherTime(data.forecastTimes[i]);
          document.getElementById("forecastWindSpeed" + i).innerHTML = data.forecastWindSpeed[i];
          document.getElementById("forecastWindDirection" + i).innerHTML = formatWindDirection(data.forecastWindDirection[i]);
          document.getElementById("forecastWindGust" + i).innerHTML = data.forecastWindGust[i];
        }
      }
      
      // Weather error handling
      var weatherErrorDiv = document.getElementById("weatherErrorDiv");
      var weatherError = document.getElementById("weatherError");
      if (data.weatherError && data.weatherError !== "") {
        weatherError.innerHTML = "Error: " + data.weatherError;
        weatherErrorDiv.style.display = "block";
      } else {
        weatherErrorDiv.style.display = "none";
      }     
      

      // Redraw the skyplane and update the position of the circle
      drawSkyplane();
      drawPositions(azimuth, elevation, setpoint_az, setpoint_el);
      
      // Draw wind direction triangle (pass raw wind direction value)
      drawWindDirection(data.currentWindDirection, data.weatherDataValid === "YES");
    }
  };
  xhr.send();
}, 250);

// Wind safety control functions
function toggleWindSafety() {
  var switchState = document.getElementById("windSafetyToggle").checked;
  var xhr = new XMLHttpRequest();
  if (switchState) {
    xhr.open("GET", "/windSafetyOn", true);
  } else {
    xhr.open("GET", "/windSafetyOff", true);
  }
  xhr.onreadystatechange = function() {
    if (xhr.readyState == 4) {
      if (xhr.status == 200) {
        console.log("Wind safety setting updated: " + xhr.responseText);
      } else {
        console.error("Failed to update wind safety setting");
      }
    }
  };
  xhr.send();
}

function toggleWindBasedHome() {
  var switchState = document.getElementById("windBasedHomeToggle").checked;
  var xhr = new XMLHttpRequest();
  if (switchState) {
    xhr.open("GET", "/windBasedHomeOn", true);
  } else {
    xhr.open("GET", "/windBasedHomeOff", true);
  }
  xhr.onreadystatechange = function() {
    if (xhr.readyState == 4) {
      if (xhr.status == 200) {
        console.log("Wind-based home setting updated: " + xhr.responseText);
      } else {
        console.error("Failed to update wind-based home setting");
      }
    }
  };
  xhr.send();
}

// Function to update the log display from the browser-side buffer
function updateLogDisplay() {
  var errorTextArea = document.getElementById("errorMessages");

  // If paused, don't update display at all
  if (logScrollPaused) {
    return;
  }

  // Join all log lines and display
  if (logLines.length > 0) {
    errorTextArea.value = logLines.join('\n');
    errorTextArea.className = "has-errors";

    // Always auto-scroll when not paused
    errorTextArea.scrollTop = errorTextArea.scrollHeight;
  } else {
    errorTextArea.value = "";
    errorTextArea.className = "";
  }

  // Update button text
  updatePauseButtonText();
}

// Function to toggle pause/resume log scrolling
function toggleLogScrollPause() {
  logScrollPaused = !logScrollPaused;

  // If resuming, refresh display and scroll to bottom
  if (!logScrollPaused) {
    updateLogDisplay();
  }

  updatePauseButtonText();
}

// Function to update pause button text
function updatePauseButtonText() {
  var pauseBtn = document.getElementById('pauseScrollBtn');
  if (pauseBtn) {
    pauseBtn.innerHTML = logScrollPaused ? 'Resume' : 'Pause';
    pauseBtn.style.backgroundColor = logScrollPaused ? '#28a745' : '#ffc107';
  }
}

// Function to clear error messages (browser-side only)
function clearErrorMessages() {
  // Clear the browser-side buffer immediately - no server call needed
  logLines = [];
  updateLogDisplay();
}

// Function to sync debug level dropdown with current level
function updateDebugLevelDropdown(currentLevel) {
  var dropdown = document.getElementById("debugLevel");
  if (dropdown) {
    dropdown.value = currentLevel;

    // Update the display text
    var levelNames = ["NONE", "ERROR", "WARN", "INFO", "DEBUG", "VERBOSE"];
    var currentLevelDisplay = document.getElementById("currentDebugLevel");
    if (currentLevelDisplay && currentLevel >= 0 && currentLevel <= 5) {
      currentLevelDisplay.innerHTML = currentLevel + " (" + levelNames[currentLevel] + ")";
    }
  }
}

// Function to toggle serial output
function toggleSerialOutput() {
  var switchState = document.getElementById("disableSerialOutput").checked;
  var xhr = new XMLHttpRequest();
  xhr.open("GET", "/setSerialOutputDisabled?disabled=" + switchState, true);
  xhr.onreadystatechange = function() {
    if (xhr.readyState == 4) {
      if (xhr.status == 200) {
        console.log("Serial output setting updated: " + xhr.responseText);
      } else {
        console.error("Failed to update serial output setting");
      }
    }
  };
  xhr.send();
}

// Simple page load handler
document.addEventListener('DOMContentLoaded', function() {
  // Page loaded - any initialization can go here
});

var canvas = document.getElementById("skyplaneCanvas");
var ctx = canvas.getContext("2d");

// Skyplane dimensions
var centerX = canvas.width / 2;
var centerY = canvas.height / 2;
var radius = Math.min(centerX, centerY) - 20;

// Function to draw the skyplane grid
function drawSkyplane() {
  // Clear and prepare canvas
  ctx.clearRect(0, 0, canvas.width, canvas.height);

  // Set common styles
  ctx.textAlign = "center";
  ctx.font = "16px Arial";

  // Draw concentric circles (elevation levels)
  for (let i = 4; i >= 0; i--) {
    const currentRadius = radius * (i / 4);
    ctx.beginPath();
    ctx.arc(centerX, centerY, currentRadius, 0, 2 * Math.PI);
    ctx.strokeStyle = i === 4 ? 'black' : 'gray';
    ctx.stroke();
  }

  // Draw azimuth lines (every 45 degrees)
  ctx.beginPath();
  for (let az = 0; az < 360; az += 45) {
    const rad = az * (Math.PI / 180);
    const x = centerX + radius * Math.cos(rad);
    const y = centerY + radius * Math.sin(rad);
    ctx.moveTo(centerX, centerY);
    ctx.lineTo(x, y);
  }
  ctx.strokeStyle = 'gray';
  ctx.stroke();

  // Add cardinal direction labels
  ctx.fillStyle = "black";
  const directions = [
    { text: "N", x: centerX, y: centerY - radius - 10 },
    { text: "E", x: centerX + radius + 10, y: centerY + 5 },
    { text: "S", x: centerX, y: centerY + radius + 20 },
    { text: "W", x: centerX - radius - 10, y: centerY + 5 }
  ];

  directions.forEach(dir => {
    ctx.fillText(dir.text, dir.x, dir.y);
  });
}

// Function to draw the current AZ-EL position and the setpoint as a hollow circle
function drawPositions(azimuth, elevation, setpoint_az, setpoint_el) {
  // Normalize elevation if it's close to 360
  if (elevation >= 350) {
    elevation = 0;
  }

  // Constants for calculations
  const toRadians = Math.PI / 180;
  const adjustedAzimuth = -90 * toRadians; // Common offset

  // Calculate positions for both current and setpoint
  const positions = [
    {
      az: azimuth * toRadians + adjustedAzimuth,
      el: 1 - (elevation / 90),
      radius: 8,
      color: 'blue',
      fill: false
    },
    {
      az: setpoint_az * toRadians + adjustedAzimuth,
      el: 1 - (setpoint_el / 90),
      radius: 5,
      color: 'red',
      fill: true
    }
  ];

  // Process both points with the same logic
  positions.forEach(pos => {
    // Calculate coordinates
    let x = centerX + (radius * pos.el) * Math.cos(pos.az);
    let y = centerY + (radius * pos.el) * Math.sin(pos.az);

    // Ensure point stays within bounds
    const distanceFromCenter = Math.hypot(x - centerX, y - centerY);
    if (distanceFromCenter > radius) {
      const scale = radius / distanceFromCenter;
      x = centerX + (x - centerX) * scale;
      y = centerY + (y - centerY) * scale;
    }

    // Draw the point
    ctx.beginPath();
    ctx.arc(x, y, pos.radius, 0, 2 * Math.PI);

    if (pos.fill) {
      ctx.fillStyle = pos.color;
      ctx.fill();
    } else {
      ctx.strokeStyle = pos.color;
      ctx.lineWidth = 2;
      ctx.stroke();
    }
  });
}

// Function to draw wind direction triangle
function drawWindDirection(windDirection, weatherDataValid) {
  // Don't draw if no valid wind data
  if (!weatherDataValid || !windDirection || windDirection === "N/A" || isNaN(windDirection)) {
    return;
  }
  
  const windDir = parseFloat(windDirection);
  const windRad = (windDir - 90) * (Math.PI / 180); // Convert to radians and adjust for canvas coordinates
  const triangleRadius = radius * 1.08; // Place triangle just outside the outer radius
  const triangleSize = 8;
  
  // Calculate triangle center point (positioned on wind direction)
  const centerTriangleX = centerX + triangleRadius * Math.cos(windRad);
  const centerTriangleY = centerY + triangleRadius * Math.sin(windRad);
  
  // Create triangle points (pointing towards center, showing where wind comes from)
  const point1X = centerTriangleX + triangleSize * Math.cos(windRad);
  const point1Y = centerTriangleY + triangleSize * Math.sin(windRad);
  
  const point2X = centerTriangleX - triangleSize * 0.5 * Math.cos(windRad) + triangleSize * 0.866 * Math.cos(windRad + Math.PI/2);
  const point2Y = centerTriangleY - triangleSize * 0.5 * Math.sin(windRad) + triangleSize * 0.866 * Math.sin(windRad + Math.PI/2);
  
  const point3X = centerTriangleX - triangleSize * 0.5 * Math.cos(windRad) - triangleSize * 0.866 * Math.cos(windRad + Math.PI/2);
  const point3Y = centerTriangleY - triangleSize * 0.5 * Math.sin(windRad) - triangleSize * 0.866 * Math.sin(windRad + Math.PI/2);
  
  // Draw filled triangle
  ctx.beginPath();
  ctx.moveTo(point1X, point1Y);
  ctx.lineTo(point2X, point2Y);
  ctx.lineTo(point3X, point3Y);
  ctx.closePath();
  ctx.fillStyle = '#FF6B35'; // Orange-red color for visibility
  ctx.fill();
  
  // Add subtle border to triangle
  ctx.strokeStyle = '#E55A2B'; // Slightly darker orange for border
  ctx.lineWidth = 1;
  ctx.stroke();
}

// Initial draw
drawSkyplane();

function calEl() {
  var xhr = new XMLHttpRequest();
  xhr.open("GET", "/calEl", true);  // Send the value to the server via GET request
  xhr.send();
}

function moveEl(value) {
  var xhr = new XMLHttpRequest();
  xhr.open("GET", "/moveEl?value=" + value, true);  // Send the value to the server via GET request
  xhr.send();
}

function moveAz(value) {
  var xhr = new XMLHttpRequest();
  xhr.open("GET", "/moveAz?value=" + value, true);  // Send the value to the server via GET request
  xhr.send();
}

function toggleCal() {
  var switchState = document.getElementById("toggleCal").checked;
  var xhr = new XMLHttpRequest();
  if (switchState) {
    xhr.open("GET", "/calon", true);
  } else {
    xhr.open("GET", "/caloff", true);
  }
  xhr.send();
}

function setSingleMotorMode() {
  var switchState = document.getElementById("singleMotorMode").checked;
  var xhr = new XMLHttpRequest();
  if (switchState) {
    xhr.open("GET", "/setSingleMotorModeOn", true);
  } else {
    xhr.open("GET", "/setSingleMotorModeOff", true);
  }
  xhr.send();
}

function stellariumOn() {
  var switchState = document.getElementById("stellariumOn").checked;
  var xhr = new XMLHttpRequest();
  if (switchState) {
    xhr.open("GET", "/stellariumOn", true);
  } else {
    xhr.open("GET", "/stellariumOff", true);
  }
  xhr.send();
}

function updateVariable() {
  var azValue = parseFloat(document.getElementById("new_setpoint_az").value);
  var elValue = parseFloat(document.getElementById("new_setpoint_el").value);

  var errorMessageAz = document.getElementById("error_message_az");
  var errorMessageEl = document.getElementById("error_message_el");

  var valid = true;

  // Validate Azimuth
  if (isNaN(azValue) || azValue < -360 || azValue > 360) {
    errorMessageAz.textContent = "Please enter a valid Azimuth between -360 and 360.";
    valid = false;
  } else {
    errorMessageAz.textContent = "";
  }

  // Validate Elevation
  if (isNaN(elValue) || elValue < 0 || elValue > 90) {
    errorMessageEl.textContent = "Please enter a valid Elevation between 0 and 90.";
    valid = false;
  } else {
    errorMessageEl.textContent = "";
  }

  // If validation fails, prevent form submission
  if (!valid) {
    return false;
  }

  // Round to 1 decimal place for display and consistency
  document.getElementById("new_setpoint_az").value = azValue.toFixed(2);
  document.getElementById("new_setpoint_el").value = elValue.toFixed(2);

  // Otherwise, allow form submission
  return true;
}

function submitHome() {
  // Set azimuth and elevation values to 0
  //document.getElementById('new_setpoint_az').value = 0;
  //document.getElementById('new_setpoint_el').value = 0;

  // Submit the form
  //document.getElementById('az-el-form').submit();
  
    var form = document.createElement('form');
    form.method = 'POST';
    form.action = '/submitHome';
    document.body.appendChild(form);
    form.submit();  
}

// Function to toggle hotspot mode
function toggleHotspotMode() {
  var hotspotCheckbox = document.getElementById("hotspot");
  var ssidField = document.getElementById("ssid");
  var passwordField = document.getElementById("password");

  if (hotspotCheckbox.checked) {
    // Grey out the SSID and Password fields and display default values
    ssidField.value = "discoverydish_HOTSPOT";
    passwordField.value = "discoverydish";
    ssidField.disabled = true;
    passwordField.disabled = true;
  } else {
    // Enable SSID and Password fields and clear default values
    ssidField.value = "";
    passwordField.value = "";
    ssidField.disabled = false;
    passwordField.disabled = false;
  }
}

// Confirmation functions for critical actions
function confirmRestartESP32() {
  if (confirm("Are you sure you want to restart the ESP32?\n\nThis will temporarily disconnect the web interface and interrupt any ongoing operations.")) {
    // Create and submit a hidden form to restart the ESP32
    var form = document.createElement('form');
    form.method = 'POST';
    form.action = '/restart';
    document.body.appendChild(form);
    form.submit();
  }
}

function confirmResetNeedsUnwind() {
  if (confirm("Are you sure you want to reset Needs Unwind flag?\n\nThis could cause the rotator to over rotate and tangle cables.")) {
    // Create and submit a hidden form to restart the ESP32
    var form = document.createElement('form');
    form.method = 'POST';
    form.action = '/resetNeedsUnwind';
    document.body.appendChild(form);
    form.submit();
  }
}

function confirmResetEEPROM() {
  if (confirm("WARNING: Are you sure you want to reset the EEPROM?\n\nThis will erase all saved settings and return the device to factory defaults. This action cannot be undone.")) {
    // Create and submit a hidden form to reset EEPROM
    var form = document.createElement('form');
    form.method = 'POST';
    form.action = '/resetEEPROM';
    document.body.appendChild(form);
    form.submit();
  }
}

function setDebugLevel() {
  var debugLevel = document.getElementById("debugLevel").value;
  var xhr = new XMLHttpRequest();
  xhr.open("POST", "/setDebugLevel", true);
  xhr.setRequestHeader("Content-Type", "application/x-www-form-urlencoded");

  xhr.onreadystatechange = function() {
    if (xhr.readyState == 4) {
      if (xhr.status == 204) {
        console.log("Debug level updated successfully");
      } else {
        console.error("Failed to update debug level");
      }
    }
  };

  xhr.send("debugLevel=" + debugLevel);
  return false; // Prevent form submission
}

// Function to toggle advanced parameters visibility
function toggleAdvancedParams() {
  var checkbox = document.getElementById("showAdvanced");
  var section = document.getElementById("advancedParamsSection");

  if (checkbox.checked) {
    section.style.display = "block";
  } else {
    section.style.display = "none";
  }
}

// Weather control functions
function toggleWeatherPolling() {
  var switchState = document.getElementById("weatherPolling").checked;
  var xhr = new XMLHttpRequest();
  if (switchState) {
    xhr.open("GET", "/weatherOn", true);
  } else {
    xhr.open("GET", "/weatherOff", true);
  }
  xhr.onreadystatechange = function() {
    if (xhr.readyState == 4) {
      if (xhr.status == 200) {
        console.log("Weather polling setting updated: " + xhr.responseText);
      } else {
        console.error("Failed to update weather polling setting");
      }
    }
  };
  xhr.send();
}

function forceWeatherUpdate() {
  var xhr = new XMLHttpRequest();
  xhr.open("GET", "/forceWeatherUpdate", true);
  xhr.onreadystatechange = function() {
    if (xhr.readyState == 4) {
      if (xhr.status == 200) {
        alert("Weather update requested. Data will refresh shortly.");
      } else {
        var errorMsg = "Failed to force weather update";
        if (xhr.responseText) {
          errorMsg += ": " + xhr.responseText;
        }
        if (xhr.responseText.includes("Location not configured")) {
          errorMsg += "\nPlease set your location first.";
        } else if (xhr.responseText.includes("API key not configured")) {
          errorMsg += "\nPlease set your WeatherAPI.com API key first.";
        }
        alert(errorMsg);
      }
    }
  };
  xhr.send();
}


function getMyLocation() {
  if (navigator.geolocation) {
    navigator.geolocation.getCurrentPosition(
      function(position) {
        var lat = position.coords.latitude;
        var lon = position.coords.longitude;
        
        document.getElementById('latitude').value = lat.toFixed(6);
        document.getElementById('longitude').value = lon.toFixed(6);
        
        alert("Location detected: " + lat.toFixed(6) + ", " + lon.toFixed(6) + 
              "\nClick 'Set Location' to save these coordinates.");
      },
      function(error) {
        var errorMsg = "";
        switch(error.code) {
          case error.PERMISSION_DENIED:
            errorMsg = "Location access denied by user.";
            break;
          case error.POSITION_UNAVAILABLE:
            errorMsg = "Location information is unavailable.";
            break;
          case error.TIMEOUT:
            errorMsg = "Location request timed out.";
            break;
          default:
            errorMsg = "An unknown error occurred.";
            break;
        }
        alert("Error getting location: " + errorMsg);
      },
      {
        enableHighAccuracy: true,
        timeout: 10000,
        maximumAge: 300000 // 5 minutes
      }
    );
  } else {
    alert("Geolocation is not supported by this browser.");
  }
}

function formatWeatherTime(timeStr) {
  if (!timeStr || timeStr === "N/A" || timeStr === "") {
    return "N/A";
  }
  
  try {
    // WeatherAPI returns time like "2024-01-15 14:30" (local time at location)
    // Extract just the time part directly without timezone conversion
    var spaceIndex = timeStr.indexOf(' ');
    if (spaceIndex === -1) {
      return timeStr; // Return original if format is unexpected
    }
    
    var timePart = timeStr.substring(spaceIndex + 1);
    
    // Validate time format (should be HH:MM)
    if (timePart.length >= 5 && timePart.indexOf(':') !== -1) {
      return timePart; // Return local time as-is
    } else {
      return timeStr; // Return original if can't parse
    }
  } catch (e) {
    return timeStr; // Return original if any error
  }
}

function formatWindDirection(degrees) {
  if (!degrees || degrees === "N/A" || isNaN(degrees)) {
    return "N/A";
  }
  
  var dir = parseFloat(degrees);
  var directions = ["N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE", 
                   "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"];
  var index = Math.round(dir / 22.5) % 16;
  return dir.toFixed(0) + "° (" + directions[index] + ")";
}