#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <EEPROM.h>

// Конфигурация
#define NUM_SHELVES 5
#define SCHEDULE_COUNT 10
#define EEPROM_SIZE 512

// Пины реле (можно изменить под вашу схему)
const int relayPins[NUM_SHELVES] = {D1, D2, D5, D6, D7};

// Структура для расписания
struct Schedule {
  uint8_t shelf;        // Полка (0-4)
  uint8_t hourOn;       // Час включения
  uint8_t minuteOn;     // Минута включения
  uint8_t hourOff;      // Час выключения
  uint8_t minuteOff;    // Минута выключения
  uint8_t days;         // Битовая маска дней (бит 0 - понедельник)
  bool enabled;         // Активно
};

ESP8266WebServer server(80);
Schedule schedules[SCHEDULE_COUNT];
bool relayStates[NUM_SHELVES] = {false, false, false, false, false};

// HTML главной страницы
const char* mainPage = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
    <title>Управление освещением</title>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial; margin: 20px; }
        h1 { color: #333; }
        .shelf { margin: 10px 0; padding: 10px; border: 1px solid #ddd; }
        .button { 
            padding: 10px 15px; 
            margin: 5px; 
            border: none; 
            background: #4CAF50; 
            color: white; 
            cursor: pointer;
            font-size: 14px;
            border-radius: 4px;
        }
        .button.off { background: #f44336; }
        .nav { margin: 20px 0; }
        .nav a { 
            margin: 0 10px; 
            text-decoration: none; 
            color: #2196F3; 
            font-weight: bold;
        }
        .status { 
            margin: 10px 0; 
            padding: 15px; 
            background: #e3f2fd; 
            border-radius: 4px;
            border: 1px solid #bbdefb;
        }
        .config-page { max-width: 500px; margin: 0 auto; }
        .config-box { 
            padding: 20px; 
            border: 1px solid #ddd; 
            border-radius: 5px; 
            margin: 20px 0;
        }
    </style>
</head>
<body>
    <h1>Управление освещением стеллажа</h1>
    
    <div class="nav">
        <a href="/">Главная</a>
        <a href="/schedule">Расписание</a>
        <a href="/config">Настройки WiFi</a>
    </div>
    
    <div class="status">
        IP адрес: %IP%<br>
        WiFi: %WIFISTATUS%<br>
        Имя сети: %SSID%
    </div>
    
    <h2>Ручное управление:</h2>
    %SHELVES%
    
    <div style="margin-top: 20px;">
        <button class="button" onclick="controlAll(true)">Включить все</button>
        <button class="button off" onclick="controlAll(false)">Выключить все</button>
    </div>
    
    <script>
    function toggleRelay(shelf, btn) {
        var newState = btn.innerText === 'Включить';
        fetch('/control?shelf=' + shelf + '&state=' + (newState ? '1' : '0'));
        btn.innerText = newState ? 'Выключить' : 'Включить';
        btn.className = newState ? 'button off' : 'button';
    }
    
    function controlAll(state) {
        var buttons = document.querySelectorAll('[id^="btn"]');
        buttons.forEach(function(btn) {
            var shelf = btn.id.replace('btn', '');
            fetch('/control?shelf=' + shelf + '&state=' + (state ? '1' : '0'));
            btn.innerText = state ? 'Выключить' : 'Включить';
            btn.className = state ? 'button off' : 'button';
        });
    }
    </script>
</body>
</html>
)rawliteral";

// HTML страницы расписания
const char* schedulePage = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
    <title>Расписание</title>
    <meta charset="utf-8">
    <style>
        body { font-family: Arial; margin: 20px; }
        .form-group { margin: 10px 0; }
        label { display: inline-block; width: 120px; }
        input, select { padding: 5px; }
        .days label { width: auto; margin-right: 10px; }
        .schedule-item { 
            margin: 10px 0; 
            padding: 15px; 
            border: 1px solid #ccc; 
            border-radius: 4px;
            background: #f9f9f9;
        }
        .button { 
            padding: 8px 15px; 
            margin: 5px; 
            border: none; 
            background: #4CAF50; 
            color: white; 
            cursor: pointer;
            border-radius: 4px;
        }
        .button.delete { background: #f44336; }
        .back-link { display: inline-block; margin-bottom: 20px; }
    </style>
</head>
<body>
    <h1>Настройка расписания</h1>
    <a href="/" class="back-link">← Назад на главную</a>
    
    <h2>Добавить расписание:</h2>
    <div style="padding: 15px; border: 1px solid #ddd; border-radius: 4px; max-width: 500px;">
        <form id="addForm">
            <div class="form-group">
                <label>Полка:</label>
                <select id="shelf" style="width: 150px;">
                    <option value="0">Полка 1</option>
                    <option value="1">Полка 2</option>
                    <option value="2">Полка 3</option>
                    <option value="3">Полка 4</option>
                    <option value="4">Полка 5</option>
                </select>
            </div>
            
            <div class="form-group">
                <label>Время включения:</label>
                <input type="number" id="hourOn" min="0" max="23" value="8" style="width:60px"> :
                <input type="number" id="minuteOn" min="0" max="59" value="0" style="width:60px">
            </div>
            
            <div class="form-group">
                <label>Время выключения:</label>
                <input type="number" id="hourOff" min="0" max="23" value="20" style="width:60px"> :
                <input type="number" id="minuteOff" min="0" max="59" value="0" style="width:60px">
            </div>
            
            <div class="form-group days">
                <label style="display:block; margin-bottom:5px;">Дни недели:</label>
                <label><input type="checkbox" name="day" value="1" checked> Пн</label>
                <label><input type="checkbox" name="day" value="2" checked> Вт</label>
                <label><input type="checkbox" name="day" value="4" checked> Ср</label>
                <label><input type="checkbox" name="day" value="8" checked> Чт</label>
                <label><input type="checkbox" name="day" value="16" checked> Пт</label>
                <label><input type="checkbox" name="day" value="32"> Сб</label>
                <label><input type="checkbox" name="day" value="64"> Вс</label>
            </div>
            
            <div class="form-group">
                <label><input type="checkbox" id="enabled" checked> Активно</label>
            </div>
            
            <button type="button" class="button" onclick="addSchedule()">Добавить расписание</button>
        </form>
    </div>
    
    <h2>Текущие расписания:</h2>
    <div id="schedules">
        %SCHEDULES%
    </div>
    
    <script>
    function addSchedule() {
        let shelf = document.getElementById('shelf').value;
        let hourOn = document.getElementById('hourOn').value;
        let minuteOn = document.getElementById('minuteOn').value;
        let hourOff = document.getElementById('hourOff').value;
        let minuteOff = document.getElementById('minuteOff').value;
        let enabled = document.getElementById('enabled').checked ? 1 : 0;
        
        // Собираем дни недели в битовую маску
        let days = 0;
        document.getElementsByName('day').forEach(cb => {
            if(cb.checked) days += parseInt(cb.value);
        });
        
        let url = '/addschedule?shelf=' + shelf + 
                 '&hourOn=' + hourOn + '&minuteOn=' + minuteOn +
                 '&hourOff=' + hourOff + '&minuteOff=' + minuteOff +
                 '&days=' + days + '&enabled=' + enabled;
        
        fetch(url).then(() => location.reload());
    }
    
    function deleteSchedule(index) {
        if(confirm('Удалить расписание?')) {
            fetch('/deleteschedule?index=' + index).then(() => location.reload());
        }
    }
    </script>
</body>
</html>
)rawliteral";

// HTML страницы настроек WiFi
const char* configPage = R"rawliteral(
<!DOCTYPE HTML>
<html>
<head>
    <title>Настройки WiFi</title>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial; margin: 20px; }
        .config-box { 
            max-width: 500px; 
            margin: 0 auto; 
            padding: 20px; 
            border: 1px solid #ddd; 
            border-radius: 5px;
        }
        .info-box {
            padding: 15px;
            background: #e3f2fd;
            border-radius: 4px;
            margin: 20px 0;
        }
        .button { 
            padding: 12px 20px; 
            margin: 10px 5px; 
            border: none; 
            background: #4CAF50; 
            color: white; 
            cursor: pointer;
            font-size: 16px;
            border-radius: 4px;
            width: 100%;
            box-sizing: border-box;
        }
        .button.reset { background: #f44336; }
        .button.wifi { background: #2196F3; }
        .back-link { display: inline-block; margin-bottom: 20px; }
        .warning {
            background: #ffebee;
            color: #c62828;
            padding: 10px;
            border-radius: 4px;
            margin: 15px 0;
        }
    </style>
</head>
<body>
    <h1>Настройки WiFi</h1>
    <a href="/" class="back-link">← Назад на главную</a>
    
    <div class="config-box">
        <div class="info-box">
            <strong>Текущие настройки:</strong><br>
            SSID: %CURRENT_SSID%<br>
            Статус: %WIFI_STATUS%<br>
            IP: %CURRENT_IP%
        </div>
        
        <div class="warning">
            <strong>Внимание:</strong> После настройки WiFi устройство перезагрузится.
        </div>
        
        <h3>Действия:</h3>
        
        <p><button class="button wifi" onclick="startWiFiConfig()">Настроить WiFi подключение</button></p>
        
        <p><button class="button reset" onclick="resetWiFi()">Сбросить настройки WiFi</button></p>
        
        <p><small>Для настройки подключитесь к точке доступа "ShelfLight" (пароль: 12345678) и откройте 192.168.4.1</small></p>
    </div>
    
    <script>
    function startWiFiConfig() {
        if(confirm('Устройство перейдет в режим настройки WiFi. Продолжить?')) {
            fetch('/startwificonfig').then(() => {
                alert('Переход в режим настройки WiFi. Подключитесь к точке доступа "ShelfLight"');
            });
        }
    }
    
    function resetWiFi() {
        if(confirm('ВНИМАНИЕ: Все настройки WiFi будут сброшены! Продолжить?')) {
            fetch('/resetwifi').then(() => {
                alert('Настройки WiFi сброшены. Устройство перезагружается...');
            });
        }
    }
    </script>
</body>
</html>
)rawliteral";

// Функции работы с EEPROM
void saveSchedules() {
    EEPROM.begin(EEPROM_SIZE);
    
    // Сохраняем количество активных расписаний
    int count = 0;
    for(int i = 0; i < SCHEDULE_COUNT; i++) {
        if(schedules[i].enabled) count++;
    }
    EEPROM.write(0, count);
    
    // Сохраняем каждое активное расписание
    int addr = 1;
    for(int i = 0; i < SCHEDULE_COUNT; i++) {
        if(schedules[i].enabled) {
            EEPROM.write(addr++, i); // Сохраняем индекс для порядка
            EEPROM.write(addr++, schedules[i].shelf);
            EEPROM.write(addr++, schedules[i].hourOn);
            EEPROM.write(addr++, schedules[i].minuteOn);
            EEPROM.write(addr++, schedules[i].hourOff);
            EEPROM.write(addr++, schedules[i].minuteOff);
            EEPROM.write(addr++, schedules[i].days);
        }
    }
    
    EEPROM.commit();
    Serial.println("Расписания сохранены");
}

void loadSchedules() {
    EEPROM.begin(EEPROM_SIZE);
    
    // Очищаем все расписания
    for(int i = 0; i < SCHEDULE_COUNT; i++) {
        schedules[i].enabled = false;
    }
    
    int count = EEPROM.read(0);
    if(count > SCHEDULE_COUNT) count = 0;
    
    int addr = 1;
    for(int i = 0; i < count; i++) {
        int index = EEPROM.read(addr++);
        if(index < SCHEDULE_COUNT) {
            schedules[index].shelf = EEPROM.read(addr++);
            schedules[index].hourOn = EEPROM.read(addr++);
            schedules[index].minuteOn = EEPROM.read(addr++);
            schedules[index].hourOff = EEPROM.read(addr++);
            schedules[index].minuteOff = EEPROM.read(addr++);
            schedules[index].days = EEPROM.read(addr++);
            schedules[index].enabled = true;
        } else {
            addr += 6; // Пропускаем 6 байт
        }
    }
    
    Serial.println("Загружено расписаний: " + String(count));
}

// Обработчики веб-сервера
void handleRoot() {
    String html = mainPage;
    html.replace("%IP%", WiFi.localIP().toString());
    html.replace("%WIFISTATUS%", WiFi.status() == WL_CONNECTED ? "Подключено" : "Нет подключения");
    html.replace("%SSID%", WiFi.status() == WL_CONNECTED ? WiFi.SSID() : "Не подключено");
    
    String shelvesHtml = "";
    for(int i = 0; i < NUM_SHELVES; i++) {
        shelvesHtml += "<div class='shelf'>";
        shelvesHtml += "<strong>Полка " + String(i+1) + ":</strong> ";
        shelvesHtml += "<button id='btn" + String(i) + "' class='";
        shelvesHtml += relayStates[i] ? "button off" : "button";
        shelvesHtml += "' onclick='toggleRelay(" + String(i) + ", this)'>";
        shelvesHtml += relayStates[i] ? "Выключить" : "Включить";
        shelvesHtml += "</button></div>";
    }
    
    html.replace("%SHELVES%", shelvesHtml);
    server.send(200, "text/html", html);
}

void handleControl() {
    if(server.hasArg("shelf") && server.hasArg("state")) {
        int shelf = server.arg("shelf").toInt();
        bool state = server.arg("state") == "1";
        
        if(shelf >= 0 && shelf < NUM_SHELVES) {
            relayStates[shelf] = state;
            digitalWrite(relayPins[shelf], state ? LOW : HIGH); // LOW включает реле (активный низкий уровень)
            Serial.println("Полка " + String(shelf+1) + ": " + (state ? "ВКЛ" : "ВЫКЛ"));
        }
    }
    server.send(200, "text/plain", "OK");
}

void handleSchedule() {
    String html = schedulePage;
    
    String schedulesHtml = "";
    String dayNames[] = {"Пн", "Вт", "Ср", "Чт", "Пт", "Сб", "Вс"};
    
    for(int i = 0; i < SCHEDULE_COUNT; i++) {
        if(schedules[i].enabled) {
            schedulesHtml += "<div class='schedule-item'>";
            schedulesHtml += "<strong>Полка " + String(schedules[i].shelf + 1) + "</strong><br>";
            
            // Исправленная конкатенация строк
            schedulesHtml += String("Вкл: ") + 
                           (schedules[i].hourOn < 10 ? "0" : "") + 
                           String(schedules[i].hourOn) + ":" + 
                           (schedules[i].minuteOn < 10 ? "0" : "") + 
                           String(schedules[i].minuteOn);
            
            schedulesHtml += String(" | Выкл: ") + 
                           (schedules[i].hourOff < 10 ? "0" : "") + 
                           String(schedules[i].hourOff) + ":" + 
                           (schedules[i].minuteOff < 10 ? "0" : "") + 
                           String(schedules[i].minuteOff);
            
            schedulesHtml += "<br>Дни: ";
            
            for(int d = 0; d < 7; d++) {
                if(schedules[i].days & (1 << d)) {
                    schedulesHtml += dayNames[d] + " ";
                }
            }
            
            schedulesHtml += "<br><button class='button delete' onclick='deleteSchedule(" + String(i) + ")'>Удалить</button>";
            schedulesHtml += "</div>";
        }
    }
    
    if(schedulesHtml == "") {
        schedulesHtml = "<p>Нет активных расписаний</p>";
    }
    
    html.replace("%SCHEDULES%", schedulesHtml);
    server.send(200, "text/html", html);
}

void handleAddSchedule() {
    if(server.hasArg("shelf") && server.hasArg("hourOn")) {
        // Находим свободный слот
        int freeIndex = -1;
        for(int i = 0; i < SCHEDULE_COUNT; i++) {
            if(!schedules[i].enabled) {
                freeIndex = i;
                break;
            }
        }
        
        if(freeIndex == -1) {
            server.send(200, "text/plain", "Достигнут лимит расписаний");
            return;
        }
        
        schedules[freeIndex].shelf = server.arg("shelf").toInt();
        schedules[freeIndex].hourOn = server.arg("hourOn").toInt();
        schedules[freeIndex].minuteOn = server.arg("minuteOn").toInt();
        schedules[freeIndex].hourOff = server.arg("hourOff").toInt();
        schedules[freeIndex].minuteOff = server.arg("minuteOff").toInt();
        schedules[freeIndex].days = server.arg("days").toInt();
        schedules[freeIndex].enabled = server.arg("enabled").toInt() == 1;
        
        saveSchedules();
        server.send(200, "text/plain", "OK");
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
    String html = configPage;
    
    String wifiStatus = "";
    String currentSSID = "";
    String currentIP = "";
    
    if(WiFi.status() == WL_CONNECTED) {
        wifiStatus = "Подключено";
        currentSSID = WiFi.SSID();
        currentIP = WiFi.localIP().toString();
    } else {
        wifiStatus = "Не подключено";
        currentSSID = "Нет подключения";
        currentIP = "Нет IP";
    }
    
    html.replace("%WIFI_STATUS%", wifiStatus);
    html.replace("%CURRENT_SSID%", currentSSID);
    html.replace("%CURRENT_IP%", currentIP);
    
    server.send(200, "text/html", html);
}

void handleStartWiFiConfig() {
    server.send(200, "text/html", 
        "<html><body style='font-family: Arial; padding: 20px;'>"
        "<h1>Настройка WiFi</h1>"
        "<p>Устройство переходит в режим настройки WiFi.</p>"
        "<p>Подключитесь к точке доступа:</p>"
        "<ul>"
        "<li>Имя сети: <strong>ShelfLight</strong></li>"
        "<li>Пароль: <strong>12345678</strong></li>"
        "</ul>"
        "<p>Откройте браузер и перейдите по адресу: <strong>192.168.4.1</strong></p>"
        "<p style='color: #f44336;'>Через 5 секунд устройство перезагрузится...</p>"
        "</body></html>");
    
    delay(5000);
    
    // Запускаем WiFiManager в режиме конфигурации
    WiFiManager wifiManager;
    wifiManager.setTimeout(180);
    
    if(!wifiManager.startConfigPortal("ShelfLight", "12345678")) {
        Serial.println("Не удалось запустить портал конфигурации");
    }
    
    ESP.restart();
}

void handleResetWiFi() {
    server.send(200, "text/html", 
        "<html><body style='font-family: Arial; padding: 20px;'>"
        "<h1>Сброс настроек WiFi</h1>"
        "<p>Настройки WiFi сброшены.</p>"
        "<p>Устройство перезагружается...</p>"
        "<p>После перезагрузки подключитесь к точке доступа <strong>ShelfLight</strong> для настройки.</p>"
        "</body></html>");
    
    delay(1000);
    WiFiManager wifiManager;
    wifiManager.resetSettings();
    ESP.restart();
}

// Функция проверки расписаний
void checkSchedules() {
    static unsigned long lastCheck = 0;
    if(millis() - lastCheck < 60000) return; // Проверяем каждую минуту
    lastCheck = millis();
    
    // Получаем текущее время
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    if(timeinfo->tm_year < 121) return; // Если время не установлено (год < 2021)
    
    int currentHour = timeinfo->tm_hour;
    int currentMinute = timeinfo->tm_min;
    int currentDay = (timeinfo->tm_wday + 6) % 7; // Преобразуем к 0=понедельник
    
    // Проверяем бит дня недели (сдвиг влево на номер дня)
    int dayBit = 1 << currentDay;
    
    for(int i = 0; i < SCHEDULE_COUNT; i++) {
        if(!schedules[i].enabled) continue;
        
        // Проверяем, действует ли расписание на текущий день
        if(!(schedules[i].days & dayBit)) continue;
        
        // Конвертируем время в минуты
        int currentTime = currentHour * 60 + currentMinute;
        int onTime = schedules[i].hourOn * 60 + schedules[i].minuteOn;
        int offTime = schedules[i].hourOff * 60 + schedules[i].minuteOff;
        
        bool shouldBeOn = false;
        
        if(offTime > onTime) {
            // Обычное расписание в пределах одного дня
            shouldBeOn = (currentTime >= onTime && currentTime < offTime);
        } else {
            // Расписание переходит через полночь
            shouldBeOn = (currentTime >= onTime || currentTime < offTime);
        }
        
        // Применяем состояние
        int shelf = schedules[i].shelf;
        if(shelf >= 0 && shelf < NUM_SHELVES) {
            if(relayStates[shelf] != shouldBeOn) {
                relayStates[shelf] = shouldBeOn;
                digitalWrite(relayPins[shelf], shouldBeOn ? LOW : HIGH);
                Serial.println("По расписанию: Полка " + String(shelf+1) + " " + 
                             (shouldBeOn ? "ВКЛ" : "ВЫКЛ"));
            }
        }
    }
}

void setup() {
    Serial.begin(115200);
    Serial.println("\nСтарт системы освещения");
    
    // Инициализация пинов реле
    for(int i = 0; i < NUM_SHELVES; i++) {
        pinMode(relayPins[i], OUTPUT);
        digitalWrite(relayPins[i], HIGH); // Выключаем реле (активный низкий уровень)
    }
    
    // Настройка времени
    configTime(3 * 3600, 0, "pool.ntp.org", "time.nist.gov");
    
    // Загрузка расписаний из EEPROM
    loadSchedules();
    
    // Настройка WiFi через WiFiManager
    WiFiManager wifiManager;
    wifiManager.setTimeout(180);
    
    if(!wifiManager.autoConnect("ShelfLight", "12345678")) {
        Serial.println("Не удалось подключиться, запуск точки доступа");
        delay(3000);
        ESP.restart();
    }
    
    Serial.println("WiFi подключен!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    
    // Настройка веб-сервера
    server.on("/", handleRoot);
    server.on("/control", handleControl);
    server.on("/schedule", handleSchedule);
    server.on("/addschedule", handleAddSchedule);
    server.on("/deleteschedule", handleDeleteSchedule);
    server.on("/config", handleConfig);
    server.on("/startwificonfig", handleStartWiFiConfig);
    server.on("/resetwifi", handleResetWiFi);
    server.begin();
    
    Serial.println("Веб-сервер запущен");
}

void loop() {
    server.handleClient();
    checkSchedules();
}
