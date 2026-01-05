#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <EEPROM.h>
#include <time.h>

// Configuration
#define NUM_RELAYS 5
#define SCHEDULE_COUNT 10
#define EEPROM_SIZE 512
#define TIME_ZONE 3  // UTC+3 для Москвы

// Relay pins
const uint8_t relayPins[NUM_RELAYS] = {D1, D2, D5, D6, D7};

// Schedule structure
struct Schedule {
  uint8_t relay : 3;      // 3 bits для 5 реле (0-4)
  uint8_t hourOn : 5;     // 5 bits (0-23)
  uint8_t minuteOn : 6;   // 6 bits (0-59)
  uint8_t hourOff : 5;    // 5 bits
  uint8_t minuteOff : 6;  // 6 bits
  uint8_t days : 7;       // 7 bits для дней недели
  bool enabled : 1;
} __attribute__((packed));

ESP8266WebServer server(80);
Schedule schedules[SCHEDULE_COUNT];
bool relayStates[NUM_RELAYS] = {false, false, false, false, false};

// HTML main page
const char mainPage[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
    <title>Relay Controller</title>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #f5f5f5; }
        .container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 4px 12px rgba(0,0,0,0.1); }
        h1 { color: #333; border-bottom: 2px solid #4CAF50; padding-bottom: 10px; }
        .relay { margin: 15px 0; padding: 15px; border: 1px solid #ddd; border-radius: 5px; background: #f9f9f9; }
        .button { 
            padding: 10px 20px; 
            margin: 5px; 
            border: none; 
            background: #4CAF50; 
            color: white; 
            cursor: pointer;
            font-size: 14px;
            border-radius: 4px;
            min-width: 120px;
        }
        .button:hover { opacity: 0.9; }
        .button.off { background: #f44336; }
        .nav { margin: 20px 0; padding: 10px; background: #e3f2fd; border-radius: 4px; }
        .nav a { 
            margin: 0 15px; 
            text-decoration: none; 
            color: #2196F3; 
            font-weight: bold;
            padding: 8px 16px;
            border-radius: 4px;
        }
        .nav a:hover { background: #bbdefb; }
        .status { 
            margin: 15px 0; 
            padding: 15px; 
            background: #e8f5e9; 
            border-radius: 4px;
            border: 1px solid #c8e6c9;
        }
        .relay-number { width: 120px; font-weight: bold; font-size: 16px; display: inline-block; }
        .relay-status { margin-left: 20px; padding: 5px 10px; border-radius: 4px; font-weight: bold; display: inline-block; }
        .status-on { background: #c8e6c9; color: #2e7d32; }
        .status-off { background: #ffebee; color: #c62828; }
        .actions { margin-top: 30px; padding-top: 20px; border-top: 1px solid #eee; }
        .group-button { padding: 12px 24px; font-size: 16px; margin-right: 15px; }
        .header { display: flex; justify-content: space-between; align-items: center; }
        .device-info { font-size: 14px; color: #666; }
        .current-time { 
            background: #2196F3; 
            color: white; 
            padding: 10px; 
            border-radius: 5px; 
            text-align: center;
            margin: 15px 0;
            font-family: monospace;
            font-size: 18px;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>Universal Relay Controller</h1>
            <div class="device-info">Device ID: %DEVICE_ID%</div>
        </div>
        
        <div class="nav">
            <a href="/">Dashboard</a>
            <a href="/schedule">Schedules</a>
            <a href="/config">WiFi Settings</a>
        </div>
        
        <div class="current-time" id="currentTime">
            Loading time...
        </div>
        
        <div class="status">
            <strong>System Status:</strong><br>
            IP Address: %IP%<br>
            WiFi: %WIFISTATUS%<br>
            Network: %SSID%<br>
            Uptime: %UPTIME%
        </div>
        
        <h2>Manual Relay Control:</h2>
        %RELAYS%
        
        <div class="actions">
            <h3>Group Actions:</h3>
            <button class="button group-button" onclick="controlAll(true)">Turn All Relays ON</button>
            <button class="button off group-button" onclick="controlAll(false)">Turn All Relays OFF</button>
        </div>
    </div>
    
    <script>
    // Update current time every second
    function updateTime() {
        var now = new Date();
        var dateStr = now.toLocaleDateString('en-US', { 
            weekday: 'long', 
            year: 'numeric', 
            month: 'long', 
            day: 'numeric' 
        });
        var timeStr = now.toLocaleTimeString('en-US', { hour12: false });
        document.getElementById('currentTime').innerHTML = 
            '<strong>' + dateStr + '</strong><br>' + timeStr;
    }
    
    // Update time immediately and then every second
    updateTime();
    setInterval(updateTime, 1000);
    
    function toggleRelay(relay, btn) {
        var newState = btn.innerText === 'Turn ON';
        console.log('Toggling relay ' + relay + ' to ' + (newState ? 'ON' : 'OFF'));
        
        fetch('/control?relay=' + relay + '&state=' + (newState ? '1' : '0'))
            .then(response => {
                if(response.ok) {
                    btn.innerText = newState ? 'Turn OFF' : 'Turn ON';
                    btn.className = newState ? 'button off' : 'button';
                    var statusSpan = document.getElementById('status' + relay);
                    if(statusSpan) {
                        statusSpan.innerText = newState ? 'ON' : 'OFF';
                        statusSpan.className = 'relay-status ' + (newState ? 'status-on' : 'status-off');
                    }
                } else {
                    console.error('Failed to control relay');
                }
            })
            .catch(error => {
                console.error('Error:', error);
            });
    }
    
    function controlAll(state) {
        console.log('Turning all relays ' + (state ? 'ON' : 'OFF'));
        
        for(var i = 0; i < 5; i++) {
            var btn = document.getElementById('btn' + i);
            if(btn) {
                fetch('/control?relay=' + i + '&state=' + (state ? '1' : '0'))
                    .then(response => {
                        if(response.ok) {
                            btn.innerText = state ? 'Turn OFF' : 'Turn ON';
                            btn.className = state ? 'button off' : 'button';
                            var statusSpan = document.getElementById('status' + i);
                            if(statusSpan) {
                                statusSpan.innerText = state ? 'ON' : 'OFF';
                                statusSpan.className = 'relay-status ' + (state ? 'status-on' : 'status-off');
                            }
                        }
                    });
            }
        }
    }
    </script>
</body>
</html>
)rawliteral";

// HTML schedule page
const char schedulePage[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
    <title>Relay Schedules</title>
    <meta charset="utf-8">
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #f5f5f5; }
        .container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 4px 12px rgba(0,0,0,0.1); }
        h1 { color: #333; border-bottom: 2px solid #2196F3; padding-bottom: 10px; }
        .form-group { margin: 15px 0; }
        label { display: inline-block; width: 150px; font-weight: bold; }
        input, select { padding: 8px; border: 1px solid #ddd; border-radius: 4px; font-size: 14px; }
        .schedule-item { margin: 15px 0; padding: 20px; border: 1px solid #ccc; border-radius: 4px; background: #f9f9f9; }
        .button { padding: 10px 20px; margin: 5px; border: none; background: #4CAF50; color: white; cursor: pointer; border-radius: 4px; }
        .button:hover { opacity: 0.9; }
        .button.delete { background: #f44336; }
        .button.clear { background: #9E9E9E; }
        .back-link { display: inline-block; margin-bottom: 20px; text-decoration: none; color: #2196F3; font-weight: bold; }
        .schedule-form { padding: 25px; border: 1px solid #ddd; border-radius: 4px; background: white; }
        .time-input { width: 70px; text-align: center; }
        .no-schedules { padding: 30px; text-align: center; color: #666; font-style: italic; }
        .day-checkboxes { display: flex; flex-wrap: wrap; gap: 15px; margin: 10px 0; }
        .schedule-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px; }
        .schedule-time { background: #e3f2fd; padding: 5px 10px; border-radius: 4px; margin-right: 10px; }
        .current-time-info {
            background: #e3f2fd;
            padding: 10px;
            border-radius: 5px;
            margin-bottom: 20px;
            text-align: center;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>Relay Schedules</h1>
        <a href="/" class="back-link">← Back to Dashboard</a>
        
        <div class="current-time-info">
            <strong>Current Device Time:</strong><br>
            <span id="deviceTime">Loading...</span>
        </div>
        
        <div class="schedule-form">
            <h2>Add New Schedule</h2>
            <form id="addForm">
                <div class="form-group">
                    <label>Select Relay:</label>
                    <select id="relay" style="width: 200px;">
                        <option value="0">Relay 1</option>
                        <option value="1">Relay 2</option>
                        <option value="2">Relay 3</option>
                        <option value="3">Relay 4</option>
                        <option value="4">Relay 5</option>
                    </select>
                </div>
                
                <div class="form-group">
                    <label>Turn ON Time:</label>
                    <input type="number" id="hourOn" class="time-input" min="0" max="23" value="8"> :
                    <input type="number" id="minuteOn" class="time-input" min="0" max="59" value="0">
                </div>
                
                <div class="form-group">
                    <label>Turn OFF Time:</label>
                    <input type="number" id="hourOff" class="time-input" min="0" max="23" value="20"> :
                    <input type="number" id="minuteOff" class="time-input" min="0" max="59" value="0">
                </div>
                
                <div class="form-group">
                    <label>Days of Week:</label>
                    <div class="day-checkboxes">
                        <label><input type="checkbox" name="day" value="1" checked> Mon</label>
                        <label><input type="checkbox" name="day" value="2" checked> Tue</label>
                        <label><input type="checkbox" name="day" value="4" checked> Wed</label>
                        <label><input type="checkbox" name="day" value="8" checked> Thu</label>
                        <label><input type="checkbox" name="day" value="16" checked> Fri</label>
                        <label><input type="checkbox" name="day" value="32"> Sat</label>
                        <label><input type="checkbox" name="day" value="64"> Sun</label>
                    </div>
                </div>
                
                <div class="form-group">
                    <label><input type="checkbox" id="enabled" checked> Activate schedule</label>
                </div>
                
                <button type="button" class="button" onclick="addSchedule()">Add Schedule</button>
                <button type="button" class="button clear" onclick="clearForm()">Clear Form</button>
            </form>
        </div>
        
        <h2 style="margin-top: 40px;">Active Schedules:</h2>
        <div id="schedules">
            %SCHEDULES%
        </div>
    </div>
    
    <script>
    // Fetch device time from server
    function fetchDeviceTime() {
        fetch('/gettime')
            .then(response => response.text())
            .then(timeStr => {
                document.getElementById('deviceTime').textContent = timeStr;
            })
            .catch(error => {
                document.getElementById('deviceTime').textContent = 'Error loading time';
            });
    }
    
    // Update device time every 30 seconds
    fetchDeviceTime();
    setInterval(fetchDeviceTime, 30000);
    
    function addSchedule() {
        let relay = document.getElementById('relay').value;
        let hourOn = document.getElementById('hourOn').value;
        let minuteOn = document.getElementById('minuteOn').value;
        let hourOff = document.getElementById('hourOff').value;
        let minuteOff = document.getElementById('minuteOff').value;
        let enabled = document.getElementById('enabled').checked ? 1 : 0;
        
        let days = 0;
        document.getElementsByName('day').forEach(cb => {
            if(cb.checked) days += parseInt(cb.value);
        });
        
        let url = '/addschedule?relay=' + relay + 
                 '&hourOn=' + hourOn + '&minuteOn=' + minuteOn +
                 '&hourOff=' + hourOff + '&minuteOff=' + minuteOff +
                 '&days=' + days + '&enabled=' + enabled;
        
        fetch(url).then(response => {
            if(response.ok) {
                location.reload();
            } else {
                response.text().then(text => {
                    alert('Error: ' + text);
                });
            }
        });
    }
    
    function clearForm() {
        document.getElementById('hourOn').value = '8';
        document.getElementById('minuteOn').value = '0';
        document.getElementById('hourOff').value = '20';
        document.getElementById('minuteOff').value = '0';
        document.getElementById('enabled').checked = true;
        document.getElementsByName('day').forEach(cb => {
            cb.checked = cb.value <= 16;
        });
    }
    
    function deleteSchedule(index) {
        if(confirm('Delete this schedule?')) {
            fetch('/deleteschedule?index=' + index).then(() => location.reload());
        }
    }
    </script>
</body>
</html>
)rawliteral";

// HTML WiFi settings page
const char configPage[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
    <title>WiFi Settings</title>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial, sans-serif; margin: 20px; background: #f5f5f5; }
        .container { max-width: 600px; margin: 0 auto; background: white; padding: 30px; border-radius: 10px; }
        .info-box { padding: 20px; background: #e8f5e9; border-radius: 8px; margin: 20px 0; }
        .warning-box { padding: 20px; background: #fff3e0; border-radius: 8px; margin: 20px 0; color: #e65100; }
        .button { padding: 15px 25px; margin: 15px 0; border: none; background: #4CAF50; color: white; cursor: pointer; font-size: 16px; border-radius: 6px; width: 100%; }
        .button:hover { opacity: 0.9; }
        .button.reset { background: #f44336; }
        .button.wifi { background: #2196F3; }
        .back-link { display: inline-block; margin-bottom: 20px; text-decoration: none; color: #2196F3; font-weight: bold; }
        h1 { color: #333; border-bottom: 2px solid #2196F3; padding-bottom: 10px; }
        .status-item { margin: 10px 0; padding: 8px; background: white; border-radius: 4px; }
        .status-label { display: inline-block; width: 120px; font-weight: bold; color: #555; }
    </style>
</head>
<body>
    <div class="container">
        <h1>WiFi Settings</h1>
        <a href="/" class="back-link">← Back to Dashboard</a>
        
        <div class="info-box">
            <h3>Current Connection Status:</h3>
            <div class="status-item">
                <span class="status-label">Status:</span>
                <span style="color: %WIFI_COLOR%; font-weight: bold;">%WIFI_STATUS%</span>
            </div>
            <div class="status-item">
                <span class="status-label">WiFi Network:</span>
                %CURRENT_SSID%
            </div>
            <div class="status-item">
                <span class="status-label">IP Address:</span>
                %CURRENT_IP%
            </div>
        </div>
        
        <div class="warning-box">
            <strong>⚠️ Important:</strong><br>
            Changing WiFi settings will cause the device to restart.
        </div>
        
        <h3>Actions:</h3>
        
        <button class="button wifi" onclick="startWiFiConfig()">Configure WiFi Connection</button>
        
        <button class="button reset" onclick="resetWiFi()">Reset WiFi Settings</button>
    </div>
    
    <script>
    function startWiFiConfig() {
        if(confirm('Device will enter WiFi configuration mode.\n\nConnect to network "RelayController" and open 192.168.4.1\n\nContinue?')) {
            fetch('/startwificonfig').then(() => {
                alert('Entering WiFi configuration mode.');
            });
        }
    }
    
    function resetWiFi() {
        if(confirm('⚠️ WARNING: All WiFi settings will be reset!\n\nContinue?')) {
            fetch('/resetwifi').then(() => {
                alert('WiFi settings reset. Device is restarting...');
            });
        }
    }
    </script>
</body>
</html>
)rawliteral";

// Helper functions
String formatTime(int value) {
    if(value < 10) {
        return "0" + String(value);
    }
    return String(value);
}

String getFormattedTime() {
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    
    if(timeinfo.tm_year < 121) { // Year < 2021
        return "Time not synchronized";
    }
    
    String timeStr = String(1900 + timeinfo.tm_year) + "-" +
                    formatTime(timeinfo.tm_mon + 1) + "-" +
                    formatTime(timeinfo.tm_mday) + " " +
                    formatTime(timeinfo.tm_hour) + ":" +
                    formatTime(timeinfo.tm_min) + ":" +
                    formatTime(timeinfo.tm_sec);
    
    return timeStr;
}

String getDayName(int day) {
    String days[] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};
    if(day >= 0 && day < 7) return days[day];
    return "Unknown";
}

// EEPROM functions
void saveSchedules() {
    EEPROM.begin(EEPROM_SIZE);
    
    int count = 0;
    for(int i = 0; i < SCHEDULE_COUNT; i++) {
        if(schedules[i].enabled) count++;
    }
    
    EEPROM.write(0, count);
    Serial.println("Saving schedules count: " + String(count));
    
    int addr = 1;
    for(int i = 0; i < SCHEDULE_COUNT; i++) {
        if(schedules[i].enabled) {
            EEPROM.write(addr++, i);
            EEPROM.write(addr++, *(uint8_t*)&schedules[i]);
            EEPROM.write(addr++, *((uint8_t*)&schedules[i] + 1));
            Serial.println("Saved schedule " + String(i) + " for relay " + String(schedules[i].relay + 1));
        }
    }
    
    EEPROM.commit();
    EEPROM.end();
    Serial.println("Schedules saved to EEPROM");
}

void loadSchedules() {
    EEPROM.begin(EEPROM_SIZE);
    
    // Clear all schedules
    for(int i = 0; i < SCHEDULE_COUNT; i++) {
        schedules[i].enabled = false;
    }
    
    int count = EEPROM.read(0);
    Serial.println("Loading schedules count from EEPROM: " + String(count));
    
    int addr = 1;
    for(int i = 0; i < count && i < SCHEDULE_COUNT; i++) {
        int index = EEPROM.read(addr++);
        if(index < SCHEDULE_COUNT) {
            *(uint8_t*)&schedules[index] = EEPROM.read(addr++);
            *((uint8_t*)&schedules[index] + 1) = EEPROM.read(addr++);
            schedules[index].enabled = true;
            Serial.println("Loaded schedule " + String(index) + " for relay " + String(schedules[index].relay + 1));
        } else {
            addr += 2;
        }
    }
    
    EEPROM.end();
}

// Web server handlers
void handleRoot() {
    String html = FPSTR(mainPage);
    html.replace("%IP%", WiFi.localIP().toString());
    html.replace("%WIFISTATUS%", WiFi.status() == WL_CONNECTED ? "Connected" : "Not Connected");
    html.replace("%SSID%", WiFi.status() == WL_CONNECTED ? WiFi.SSID() : "Not Connected");
    
    unsigned long seconds = millis() / 1000;
    unsigned long minutes = seconds / 60;
    unsigned long hours = minutes / 60;
    unsigned long days = hours / 24;
    html.replace("%UPTIME%", String(days) + "d " + String(hours % 24) + "h " + String(minutes % 60) + "m");
    
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    html.replace("%DEVICE_ID%", mac.substring(6));
    
    String relaysHtml = "";
    for(int i = 0; i < NUM_RELAYS; i++) {
        relaysHtml += "<div class='relay'>";
        relaysHtml += "<span class='relay-number'>Relay " + String(i+1) + ":</span> ";
        relaysHtml += "<button id='btn" + String(i) + "' class='";
        relaysHtml += relayStates[i] ? "button off" : "button";
        relaysHtml += "' onclick='toggleRelay(" + String(i) + ", this)'>";
        relaysHtml += relayStates[i] ? "Turn OFF" : "Turn ON";
        relaysHtml += "</button>";
        relaysHtml += "<span id='status" + String(i) + "' class='relay-status ";
        relaysHtml += relayStates[i] ? "status-on'>ON" : "status-off'>OFF";
        relaysHtml += "</span>";
        relaysHtml += "</div>";
    }
    
    html.replace("%RELAYS%", relaysHtml);
    server.send(200, "text/html", html);
}

void handleControl() {
    if(server.hasArg("relay") && server.hasArg("state")) {
        int relay = server.arg("relay").toInt();
        bool state = server.arg("state") == "1";
        
        if(relay >= 0 && relay < NUM_RELAYS) {
            relayStates[relay] = state;
            digitalWrite(relayPins[relay], state ? LOW : HIGH);
            Serial.println("Manual control: Relay " + String(relay+1) + ": " + (state ? "ON" : "OFF"));
        }
    }
    server.send(200, "text/plain", "OK");
}

void handleSchedule() {
    String html = FPSTR(schedulePage);
    
    String schedulesHtml = "";
    
    for(int i = 0; i < SCHEDULE_COUNT; i++) {
        if(schedules[i].enabled) {
            schedulesHtml += "<div class='schedule-item'>";
            schedulesHtml += "<div class='schedule-header'>";
            schedulesHtml += "<strong>Relay " + String(schedules[i].relay + 1) + "</strong>";
            schedulesHtml += "<button class='button delete' onclick='deleteSchedule(" + String(i) + ")'>Delete</button>";
            schedulesHtml += "</div>";
            
            schedulesHtml += "<div style='margin: 10px 0;'>";
            schedulesHtml += "<span class='schedule-time'>ON: ";
            schedulesHtml += formatTime(schedules[i].hourOn);
            schedulesHtml += ":";
            schedulesHtml += formatTime(schedules[i].minuteOn);
            schedulesHtml += "</span>";
            
            schedulesHtml += "<span class='schedule-time'>OFF: ";
            schedulesHtml += formatTime(schedules[i].hourOff);
            schedulesHtml += ":";
            schedulesHtml += formatTime(schedules[i].minuteOff);
            schedulesHtml += "</span>";
            schedulesHtml += "</div>";
            
            schedulesHtml += "<div>Days: ";
            String dayNames[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
            bool anyDay = false;
            
            for(int d = 0; d < 7; d++) {
                if(schedules[i].days & (1 << d)) {
                    schedulesHtml += dayNames[d];
                    schedulesHtml += " ";
                    anyDay = true;
                }
            }
            
            if(!anyDay) {
                schedulesHtml += "None";
            }
            
            schedulesHtml += "</div>";
            schedulesHtml += "</div>";
        }
    }
    
    if(schedulesHtml == "") {
        schedulesHtml = "<div class='no-schedules'>No active schedules.<br>Add a schedule using the form above.</div>";
    }
    
    html.replace("%SCHEDULES%", schedulesHtml);
    server.send(200, "text/html", html);
}

void handleGetTime() {
    String timeStr = getFormattedTime();
    server.send(200, "text/plain", timeStr);
}

void handleAddSchedule() {
    if(server.hasArg("relay") && server.hasArg("hourOn")) {
        int freeIndex = -1;
        for(int i = 0; i < SCHEDULE_COUNT; i++) {
            if(!schedules[i].enabled) {
                freeIndex = i;
                break;
            }
        }
        
        if(freeIndex == -1) {
            server.send(200, "text/plain", "Schedule limit reached");
            return;
        }
        
        schedules[freeIndex].relay = server.arg("relay").toInt();
        schedules[freeIndex].hourOn = server.arg("hourOn").toInt();
        schedules[freeIndex].minuteOn = server.arg("minuteOn").toInt();
        schedules[freeIndex].hourOff = server.arg("hourOff").toInt();
        schedules[freeIndex].minuteOff = server.arg("minuteOff").toInt();
        schedules[freeIndex].days = server.arg("days").toInt();
        schedules[freeIndex].enabled = server.arg("enabled").toInt() == 1;
        
        Serial.println("Added schedule for relay " + String(schedules[freeIndex].relay + 1) + 
                      " ON: " + String(schedules[freeIndex].hourOn) + ":" + String(schedules[freeIndex].minuteOn) +
                      " OFF: " + String(schedules[freeIndex].hourOff) + ":" + String(schedules[freeIndex].minuteOff));
        
        saveSchedules();
        server.send(200, "text/plain", "Schedule added");
    }
}

void handleDeleteSchedule() {
    if(server.hasArg("index")) {
        int index = server.arg("index").toInt();
        if(index >= 0 && index < SCHEDULE_COUNT) {
            schedules[index].enabled = false;
            saveSchedules();
        }
    }
    server.send(200, "text/plain", "OK");
}

void handleConfig() {
    String html = FPSTR(configPage);
    
    String wifiStatus = WiFi.status() == WL_CONNECTED ? "Connected ✓" : "Not Connected ✗";
    String wifiColor = WiFi.status() == WL_CONNECTED ? "#4CAF50" : "#f44336";
    String currentSSID = WiFi.status() == WL_CONNECTED ? WiFi.SSID() : "Not Connected";
    String currentIP = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "No IP";
    
    html.replace("%WIFI_STATUS%", wifiStatus);
    html.replace("%WIFI_COLOR%", wifiColor);
    html.replace("%CURRENT_SSID%", currentSSID);
    html.replace("%CURRENT_IP%", currentIP);
    
    server.send(200, "text/html", html);
}

void handleStartWiFiConfig() {
    server.send(200, "text/plain", "Starting WiFi config...");
    delay(1000);
    
    WiFiManager wifiManager;
    wifiManager.setTimeout(300);
    
    if(!wifiManager.startConfigPortal("RelayController", "12345678")) {
        Serial.println("Failed to start config portal");
    }
    
    ESP.restart();
}

void handleResetWiFi() {
    server.send(200, "text/plain", "Resetting WiFi...");
    delay(1000);
    WiFiManager wifiManager;
    wifiManager.resetSettings();
    ESP.restart();
}

// Schedule checking
void checkSchedules() {
    static unsigned long lastCheck = 0;
    if(millis() - lastCheck < 10000) return; // Check every 10 seconds
    lastCheck = millis();
    
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    
    if(timeinfo.tm_year < 121) {
        Serial.println("Time not synchronized, skipping schedule check");
        return;
    }
    
    int currentHour = timeinfo.tm_hour;
    int currentMinute = timeinfo.tm_min;
    int currentDay = (timeinfo.tm_wday + 6) % 7; // Convert to 0=Monday
    int dayBit = 1 << currentDay;
    
    Serial.println("Schedule check: " + String(currentHour) + ":" + 
                  formatTime(currentMinute) + " Day: " + getDayName(currentDay));
    
    bool scheduleActive = false;
    
    for(int i = 0; i < SCHEDULE_COUNT; i++) {
        if(!schedules[i].enabled) continue;
        
        // Check if schedule applies to current day
        if(!(schedules[i].days & dayBit)) continue;
        
        int currentTime = currentHour * 60 + currentMinute;
        int onTime = schedules[i].hourOn * 60 + schedules[i].minuteOn;
        int offTime = schedules[i].hourOff * 60 + schedules[i].minuteOff;
        
        bool shouldBeOn = false;
        if(offTime > onTime) {
            // Normal schedule within same day
            shouldBeOn = (currentTime >= onTime && currentTime < offTime);
        } else {
            // Schedule crosses midnight
            shouldBeOn = (currentTime >= onTime || currentTime < offTime);
        }
        
        int relay = schedules[i].relay;
        if(relay >= 0 && relay < NUM_RELAYS) {
            if(relayStates[relay] != shouldBeOn) {
                relayStates[relay] = shouldBeOn;
                digitalWrite(relayPins[relay], shouldBeOn ? LOW : HIGH);
                Serial.println("Schedule " + String(i) + ": Relay " + String(relay+1) + 
                             " " + (shouldBeOn ? "ON" : "OFF") + 
                             " (Time: " + String(currentHour) + ":" + formatTime(currentMinute) + 
                             " Scheduled: " + String(schedules[i].hourOn) + ":" + 
                             formatTime(schedules[i].minuteOn) + "-" + 
                             String(schedules[i].hourOff) + ":" + formatTime(schedules[i].minuteOff) + ")");
                scheduleActive = true;
            }
        }
    }
    
    if(!scheduleActive) {
        Serial.println("No active schedules triggered");
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("\nStarting Relay Controller...");
    Serial.println("Initializing...");
    
    // Initialize relays
    for(int i = 0; i < NUM_RELAYS; i++) {
        pinMode(relayPins[i], OUTPUT);
        digitalWrite(relayPins[i], HIGH); // Turn OFF (active LOW)
        Serial.println("Initialized Relay " + String(i+1) + " on pin D" + String(relayPins[i]));
    }
    
    // Configure time
    configTime(TIME_ZONE * 3600, 0, "pool.ntp.org", "time.nist.gov");
    Serial.println("Configuring time...");
    delay(2000); // Wait for time sync
    
    // Check time
    time_t now = time(nullptr);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);
    if(timeinfo.tm_year < 121) {
        Serial.println("Time not synchronized yet");
    } else {
        Serial.println("Time synchronized: " + getFormattedTime());
    }
    
    // Load schedules
    loadSchedules();
    
    // WiFi setup
    WiFiManager wifiManager;
    wifiManager.setTimeout(180);
    
    if(!wifiManager.autoConnect("RelayController", "12345678")) {
        Serial.println("Failed to connect, restarting...");
        delay(3000);
        ESP.restart();
    }
    
    Serial.println("WiFi connected!");
    Serial.println("SSID: " + WiFi.SSID());
    Serial.println("IP: " + WiFi.localIP().toString());
    Serial.println("RSSI: " + String(WiFi.RSSI()) + " dBm");
    
    // Web server routes
    server.on("/", handleRoot);
    server.on("/control", handleControl);
    server.on("/schedule", handleSchedule);
    server.on("/gettime", handleGetTime);
    server.on("/addschedule", handleAddSchedule);
    server.on("/deleteschedule", handleDeleteSchedule);
    server.on("/config", handleConfig);
    server.on("/startwificonfig", handleStartWiFiConfig);
    server.on("/resetwifi", handleResetWiFi);
    server.begin();
    
    Serial.println("Web server started on port 80");
    Serial.println("System ready!");
}

void loop() {
    server.handleClient();
    checkSchedules();
}
