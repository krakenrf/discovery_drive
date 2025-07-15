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

      var azimuth = data.correctedAngle_az; // Replace with actual AZ data from Arduino
      var elevation = data.correctedAngle_el; // Replace with actual EL data from Arduino
      var setpoint_az = data.setpoint_az; // Replace with actual Setpoint AZ
      var setpoint_el = data.setpoint_el; // Replace with actual Setpoint EL

      // Redraw the skyplane and update the position of the circle
      drawSkyplane();
      drawPositions(azimuth, elevation, setpoint_az, setpoint_el);
    }
  };
  xhr.send();
}, 250);

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
      radius: 5,
      color: 'red',
      fill: true
    },
    {
      az: setpoint_az * toRadians + adjustedAzimuth,
      el: 1 - (setpoint_el / 90),
      radius: 8,
      color: 'blue',
      fill: false
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
  document.getElementById('new_setpoint_az').value = 0;
  document.getElementById('new_setpoint_el').value = 0;

  // Submit the form
  document.getElementById('az-el-form').submit();
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