#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <SparkFun_SCD4x_Arduino_Library.h>
#include <PubSubClient.h>
#include <Preferences.h>

// Wi-Fi credentials (будут перезаписаны при первом сохранении)
// #define DEFAULT_WIFI_SSID "YOUR_WIFI_SSID"
// #define DEFAULT_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// AP mode settings
#define AP_SSID "SCD41-Monitor"
#define AP_PASSWORD "12345678"
#define AP_CHANNEL 6

// Пины для ESP32-S3 (I2C)
#define I2C_SDA 8
#define I2C_SCL 9

SCD4x scd4x;
WebServer server(80);
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);
Preferences preferences;

float co2 = 0, temperature = 0, humidity = 0;
unsigned long lastSensorRead = 0;
const unsigned long sensorReadInterval = 2000;
bool sensorConnected = false;
bool isAPMode = false;
unsigned long lastWiFiAttempt = 0;
bool firstBoot = true;
bool ascSetting = true; // Сохранённое состояние ASC (чтобы begin() не включал ASC заново при каждой загрузке)

struct MQTTSettings {
    char server[64] = "192.168.1.100";
    int port = 1883;
    char username[32] = "";
    char password[32] = "";
    char topic[64] = "scd41/sensors";
    bool enabled = false;
} mqttSettings;

unsigned long lastMQTTPublish = 0;
const unsigned long mqttPublishInterval = 10000;

// HTML страница (минимизированный CSS)
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>SCD41 Монитор</title>
    <style>
        *{margin:0;padding:0;box-sizing:border-box}
        body{font-family:system-ui,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);min-height:100vh;padding:16px}
        .container{background:rgba(255,255,255,0.95);border-radius:24px;padding:20px;box-shadow:0 10px 40px rgba(0,0,0,0.2);max-width:650px;width:100%;margin:0 auto}
        .tabs{display:flex;gap:10px;margin-bottom:20px;border-bottom:2px solid #363434;padding-bottom:10px;flex-wrap:wrap}
        .tab-btn{background:none;border:none;padding:8px 16px;cursor:pointer;border-radius:8px;font-weight:500;color:rgba(0,0,0,0.2)}
        .tab-btn.active{background:linear-gradient(135deg,#667eea,#764ba2);color:white}
        .tab-content{display:none}
        .tab-content.active{display:block}
        h1{color:#333;margin-bottom:20px;font-size:1.5em;text-align:center}
        h2{color:#555;margin:16px 0 12px;font-size:1.1em}
        .sensor-card{background:#f5f7fa;border-radius:16px;padding:16px;margin:12px 0}
        .value{font-size:2em;font-weight:bold;margin:8px 0}
        .unit{font-size:0.5em;color:#666}
        .label{color:#555;margin-bottom:8px}
        .emoji{font-size:1.5em}
        .status,.mqtt-status,.calibration-status,.wifi-status{padding:8px;border-radius:8px;margin:12px 0;font-size:0.85em}
        .status-ok{background:#d4edda;color:#155724}
        .status-error{background:#f8d7da;color:#721c24}
        .status-warning{background:#fff3cd;color:#856404}
        .status-info{background:#d1ecf1;color:#0c5460}
        .co2-good{color:#28a745}
        .co2-moderate{color:#ffc107}
        .co2-bad{color:#dc3545}
        .settings-form{display:flex;flex-direction:column;gap:12px}
        .form-group{display:flex;flex-direction:column;gap:4px}
        .form-group label{font-weight:bold;color:#555}
        .form-group input{padding:8px;border:1px solid #ddd;border-radius:8px}
        .checkbox-group{flex-direction:row;align-items:center;gap:8px}
        button{background:linear-gradient(135deg,#667eea,#764ba2);color:white;border:none;padding:10px;border-radius:8px;cursor:pointer}
        button:hover{opacity:0.9}
        .danger{background:linear-gradient(135deg,#f093fb,#f5576c)}
        .warning{background:linear-gradient(135deg,#fa709a,#fee140)}
        .sensor-info{font-size:0.85em;color:#666;padding:8px;background:#e9ecef;border-radius:8px;margin:8px 0}
        .progress-bar{width:100%;height:16px;background:#e0e0e0;border-radius:8px;overflow:hidden;margin:8px 0}
        .progress-fill{height:100%;background:linear-gradient(90deg,#667eea,#764ba2);width:0%}
        .wifi-config{background:#f8f9fa;padding:15px;border-radius:10px;margin:10px 0}
        @media(max-width:600px){.container{padding:12px}.value{font-size:1.5em}}
    </style>
</head>
<body>
    <div class="container">
        <h1>🌍 SCD41 Монитор</h1>
        <div class="tabs">
            <button class="tab-btn active" onclick="switchTab('dashboard')">📊 Дашборд</button>
            <button class="tab-btn" onclick="switchTab('wifi')">📡 Wi-Fi</button>
            <button class="tab-btn" onclick="switchTab('mqtt')">📡 MQTT</button>
            <button class="tab-btn" onclick="switchTab('calibration')">🔧 Калибровка</button>
        </div>
        
        <div id="dashboard" class="tab-content active">
            <div class="sensor-card">
                <div class="emoji">💨</div>
                <div class="label">CO₂</div>
                <div class="value" id="co2">--- <span class="unit">ppm</span></div>
            </div>
            <div class="sensor-card">
                <div class="emoji">🌡️</div>
                <div class="label">Температура</div>
                <div class="value" id="temp">--- <span class="unit">°C</span></div>
            </div>
            <div class="sensor-card">
                <div class="emoji">💧</div>
                <div class="label">Влажность</div>
                <div class="value" id="hum">--- <span class="unit">%</span></div>
            </div>
            <div id="status" class="status status-ok">✅ Датчик работает</div>
            <div id="wifi-status" class="wifi-status status-info">📡 Wi-Fi: подключение...</div>
            <div id="mqtt-status" class="mqtt-status status-warning">⏳ MQTT: не настроен</div>
        </div>
        
        <div id="wifi" class="tab-content">
            <div class="wifi-config">
                <h2>🔧 Настройка Wi-Fi</h2>
                <form class="settings-form" onsubmit="saveWiFiSettings(event)">
                    <div class="form-group">
                        <label>🌐 SSID</label>
                        <input type="text" id="wifiSSID" placeholder="Wi-Fi имя" required>
                    </div>
                    <div class="form-group">
                        <label>🔑 Пароль</label>
                        <input type="password" id="wifiPassword" placeholder="Пароль">
                    </div>
                    <button type="submit">💾 Сохранить и перезагрузить</button>
                </form>
                <div id="wifi-config-status" class="status" style="margin-top:15px;"></div>
                <div class="sensor-info" style="margin-top:15px;">
                    <strong>📡 Текущий режим:</strong><br>
                    <span id="wifiModeInfo">Загрузка...</span>
                </div>
            </div>
        </div>
        
        <div id="mqtt" class="tab-content">
            <form class="settings-form" onsubmit="saveMQTTSettings(event)">
                <div class="form-group checkbox-group">
                    <input type="checkbox" id="mqttEnabled">
                    <label>✅ Включить MQTT</label>
                </div>
                <div class="form-group">
                    <label>Сервер</label>
                    <input type="text" id="mqttServer" placeholder="192.168.1.100">
                </div>
                <div class="form-group">
                    <label>Порт</label>
                    <input type="number" id="mqttPort" placeholder="1883">
                </div>
                <div class="form-group">
                    <label>Пользователь</label>
                    <input type="text" id="mqttUsername" placeholder="username">
                </div>
                <div class="form-group">
                    <label>Пароль</label>
                    <input type="password" id="mqttPassword" placeholder="password">
                </div>
                <div class="form-group">
                    <label>Топик</label>
                    <input type="text" id="mqttTopic" placeholder="scd41/sensors">
                </div>
                <button type="submit">💾 Сохранить</button>
            </form>
            <div id="mqtt-config-status" class="mqtt-status"></div>
        </div>
        
        <div id="calibration" class="tab-content">
            <div class="sensor-info">
                <strong>📖 Информация:</strong><br>
                • ASC включена по умолчанию<br>
                • Для свежего воздуха используйте 400 ppm
            </div>
            
            <h2>🔄 Автокалибровка (ASC)</h2>
            <div class="settings-form">
                <div class="form-group checkbox-group">
                    <input type="checkbox" id="ascEnabled">
                    <label>Включить ASC</label>
                </div>
                <button onclick="setASC()">💾 Применить</button>
            </div>
            
            <h2>🌿 Свежий воздух (400 ppm)</h2>
            <button onclick="calibrateFreshAir()" class="warning">🍃 Калибровка</button>
            <div id="freshAirProgress" class="progress-bar" style="display:none;"><div class="progress-fill" id="freshAirFill"></div></div>
            
            <h2>🎯 Принудительная калибровка</h2>
            <div class="settings-form">
                <div class="form-group">
                    <label>Целевое CO₂ (ppm)</label>
                    <input type="number" id="frcValue" value="400">
                </div>
                <button onclick="forceCalibration()">🔧 Выполнить</button>
                <div id="frcProgress" class="progress-bar" style="display:none;"><div class="progress-fill" id="frcFill"></div></div>
            </div>
            
            <h2>⚠️ Сброс</h2>
            <button onclick="factoryReset()" class="danger">🔄 Сброс настроек</button>
            <button onclick="restartSensor()" class="warning">🔄 Перезапуск датчика</button>
            
            <div id="calibration-status" class="calibration-status"></div>
            <div id="sensor-details" class="sensor-info"></div>
        </div>
    </div>

    <script>
        function switchTab(tabName){
            document.querySelectorAll('.tab-content').forEach(t=>t.classList.remove('active'));
            document.querySelectorAll('.tab-btn').forEach(b=>b.classList.remove('active'));
            document.getElementById(tabName).classList.add('active');
            event.target.classList.add('active');
            if(tabName==='calibration') loadCalibrationInfo();
            if(tabName==='wifi') loadWiFiInfo();
        }
        
        function getCO2Class(v){return v<800?'co2-good':v<1200?'co2-moderate':'co2-bad';}
        
        function loadWiFiInfo(){
            fetch('/api/wifi/info').then(r=>r.json()).then(d=>{
                document.getElementById('wifiModeInfo').innerHTML=`
                    Режим: ${d.ap_mode ? '🔵 Точка доступа' : '🟢 Клиент Wi-Fi'}<br>
                    ${d.ap_mode ? 
                        `SSID точки: ${d.ap_ssid}<br>
                         IP адрес: ${d.ip}<br>
                         Подключенные устройства: ${d.stations}` : 
                        `Подключен к: ${d.ssid}<br>
                         IP адрес: ${d.ip}<br>
                         Сигнал: ${d.rssi} dBm`
                    }
                `;
            });
        }
        
        function loadMQTTSettings(){
            fetch('/api/mqtt/config').then(r=>r.json()).then(d=>{
                document.getElementById('mqttEnabled').checked=d.enabled;
                document.getElementById('mqttServer').value=d.server;
                document.getElementById('mqttPort').value=d.port;
                document.getElementById('mqttUsername').value=d.username;
                document.getElementById('mqttTopic').value=d.topic;
            });
        }
        
        function loadCalibrationInfo(){
            fetch('/api/sensor/info').then(r=>r.json()).then(d=>{
                document.getElementById('sensor-details').innerHTML=`
                    <strong>Датчик:</strong><br>
                    • Серийник: ${d.serial_number||'N/A'}<br>
                    • ASC: ${d.asc_enabled?'✅ Вкл':'❌ Выкл'}<br>
                    • Статус: ${d.connected?'✅ Работает':'❌ Ошибка'}
                `;
                document.getElementById('ascEnabled').checked=d.asc_enabled;
            });
        }
        
        function saveWiFiSettings(e){
            e.preventDefault();
            fetch('/api/wifi/config',{
                method:'POST',
                headers:{'Content-Type':'application/json'},
                body:JSON.stringify({
                    ssid:document.getElementById('wifiSSID').value,
                    password:document.getElementById('wifiPassword').value
                })
            }).then(r=>r.json()).then(d=>{
                let div=document.getElementById('wifi-config-status');
                if(d.success){
                    div.className='status status-ok';
                    div.innerHTML='✅ Настройки сохранены! Перезагрузка через 3 секунды...';
                    setTimeout(()=>location.reload(),3000);
                }else{
                    div.className='status status-error';
                    div.innerHTML='❌ Ошибка сохранения';
                }
            });
        }
        
        function saveMQTTSettings(e){
            e.preventDefault();
            fetch('/api/mqtt/config',{
                method:'POST',
                headers:{'Content-Type':'application/json'},
                body:JSON.stringify({
                    enabled:document.getElementById('mqttEnabled').checked,
                    server:document.getElementById('mqttServer').value,
                    port:parseInt(document.getElementById('mqttPort').value),
                    username:document.getElementById('mqttUsername').value,
                    password:document.getElementById('mqttPassword').value,
                    topic:document.getElementById('mqttTopic').value
                })
            }).then(r=>r.json()).then(d=>{
                let div=document.getElementById('mqtt-config-status');
                if(d.success){
                    div.className='mqtt-status status-ok';
                    div.innerHTML='✅ Сохранено! Перезагрузка через 2 секунды...';
                    setTimeout(()=>location.reload(),2000);
                }else div.innerHTML='❌ Ошибка';
            });
        }
        
        function setASC(){
            fetch('/api/calibration/asc',{
                method:'POST',
                headers:{'Content-Type':'application/json'},
                body:JSON.stringify({enabled:document.getElementById('ascEnabled').checked})
            }).then(r=>r.json()).then(d=>showStatus(d));
        }
        
        function calibrateFreshAir(){
            let div=document.getElementById('calibration-status');
            div.className='calibration-status status-info';
            div.innerHTML='⏳ Калибровка... 30 сек';
            showProgress('freshAirProgress','freshAirFill');
            fetch('/api/calibration/fresh_air',{method:'POST'})
                .then(r=>r.json()).then(d=>{hideProgress('freshAirProgress');showStatus(d);});
        }
        
        function forceCalibration(){
            let val=parseInt(document.getElementById('frcValue').value);
            if(val<400||val>2000){showStatus({success:false,message:'400-2000 ppm'});return;}
            showProgress('frcProgress','frcFill');
            fetch('/api/calibration/force',{
                method:'POST',
                headers:{'Content-Type':'application/json'},
                body:JSON.stringify({value:val})
            }).then(r=>r.json()).then(d=>{hideProgress('frcProgress');showStatus(d);});
        }
        
        function factoryReset(){
            if(confirm('Сбросить калибровку?'))
                fetch('/api/calibration/factory_reset',{method:'POST'})
                    .then(r=>r.json()).then(showStatus);
        }
        
        function restartSensor(){
            fetch('/api/sensor/restart',{method:'POST'})
                .then(r=>r.json()).then(showStatus);
        }
        
        function showStatus(data){
            let div=document.getElementById('calibration-status');
            div.className=data.success?'calibration-status status-ok':'calibration-status status-error';
            div.innerHTML=(data.success?'✅ ':'❌ ')+data.message;
            setTimeout(()=>loadCalibrationInfo(),1000);
        }
        
        function showProgress(barId,fillId){
            let bar=document.getElementById(barId),fill=document.getElementById(fillId);
            bar.style.display='block';
            let p=0,iv=setInterval(()=>{p+=3.33;fill.style.width=p+'%';if(p>=100)clearInterval(iv);},1000);
        }
        
        function hideProgress(barId){
            setTimeout(()=>{
                document.getElementById(barId).style.display='none';
                let fill = document.getElementById(barId + 'Fill');
                if(fill) fill.style.width = '0%';
            },2000);
        }
        
        function updateData(){
            fetch('/api/data').then(r=>r.json()).then(d=>{
                if(d.connected){
                    let co2e=document.getElementById('co2');
                    co2e.innerHTML=Math.round(d.co2)+' <span class="unit">ppm</span>';
                    co2e.className='value '+getCO2Class(d.co2);
                    document.getElementById('temp').innerHTML=d.temp.toFixed(1)+' <span class="unit">°C</span>';
                    document.getElementById('hum').innerHTML=d.humidity.toFixed(1)+' <span class="unit">%</span>';
                    document.getElementById('status').innerHTML='✅ CO₂: '+Math.round(d.co2)+' ppm';
                }else document.getElementById('status').innerHTML='⚠️ Датчик не найден';
                
                let wifiDiv=document.getElementById('wifi-status');
                if(d.ap_mode){
                    wifiDiv.className='wifi-status status-warning';
                    wifiDiv.innerHTML='📡 РЕЖИМ ТОЧКИ ДОСТУПА: '+d.ap_ssid;
                }else{
                    wifiDiv.className='wifi-status status-ok';
                    wifiDiv.innerHTML='📡 Wi-Fi: '+d.wifi_ssid;
                }
                
                let mqttDiv=document.getElementById('mqtt-status');
                if(d.mqtt_connected) mqttDiv.className='mqtt-status status-ok',mqttDiv.innerHTML='📡 MQTT: подключен';
                else if(d.mqtt_enabled) mqttDiv.className='mqtt-status status-error',mqttDiv.innerHTML='❌ MQTT: ошибка';
                else mqttDiv.className='mqtt-status status-warning',mqttDiv.innerHTML='⏳ MQTT: отключен';
            }).catch(e=>console.error(e));
        }
        
        loadMQTTSettings();
        updateData();
        setInterval(updateData,2000);
    </script>
</body>
</html>
)rawliteral";

// Функции сохранения Wi-Fi настроек
void saveWiFiSettings(String ssid, String password) {
    preferences.begin("wifi", false);
    preferences.putString("ssid", ssid);
    preferences.putString("password", password);
    preferences.putBool("configured", true);
    preferences.end();
    Serial.println("✅ Wi-Fi настройки сохранены");
}

bool loadWiFiSettings(String &ssid, String &password) {
    preferences.begin("wifi", true);
    bool configured = preferences.getBool("configured", false);
    if(configured) {
        ssid = preferences.getString("ssid", "");
        password = preferences.getString("password", "");
        preferences.end();
        return (ssid.length() > 0);
    }
    preferences.end();
    return false;
}

// Функция запуска точки доступа
void startAccessPoint() {
    Serial.println("📡 Запуск точки доступа...");
    isAPMode = true;
    
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD, AP_CHANNEL, 0, 4);
    
    Serial.println("🔵 Точка доступа создана");
    Serial.printf("   SSID: %s\n", AP_SSID);
    Serial.printf("   Пароль: %s\n", AP_PASSWORD);
    Serial.printf("   IP адрес: %s\n", WiFi.softAPIP().toString().c_str());
    Serial.println("\n⚙️ Для настройки Wi-Fi:");
    Serial.println("   1. Подключитесь к точке доступа SCD41-Monitor");
    Serial.println("   2. Откройте браузер и перейдите по адресу http://192.168.4.1");
    Serial.println("   3. Перейдите на вкладку 'Wi-Fi' и введите данные вашей сети");
}

// Функция подключения к Wi-Fi с повторными попытками
bool connectToWiFi() {
    String savedSSID, savedPassword;
    
    // Пытаемся загрузить сохраненные настройки
    if(!loadWiFiSettings(savedSSID, savedPassword)) {
        Serial.println("⚠️ Нет сохраненных Wi-Fi настроек");
        return false;
    }
    
    Serial.printf("🔌 Подключение к Wi-Fi: %s\n", savedSSID.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(savedSSID.c_str(), savedPassword.c_str());
    
    // Даем 15 секунд на подключение
    int attempts = 0;
    while(WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }
    
    if(WiFi.status() == WL_CONNECTED) {
        isAPMode = false;
        Serial.println("\n✅ Wi-Fi подключен!");
        Serial.printf("   IP адрес: %s\n", WiFi.localIP().toString().c_str());
        Serial.printf("   Сигнал: %d dBm\n", WiFi.RSSI());
        return true;
    } else {
        Serial.println("\n❌ Ошибка подключения к Wi-Fi");
        return false;
    }
}

// Функции MQTT
void saveMQTTSettings() {
    preferences.begin("mqtt", false);
    preferences.putBool("enabled", mqttSettings.enabled);
    preferences.putString("server", mqttSettings.server);
    preferences.putInt("port", mqttSettings.port);
    preferences.putString("username", mqttSettings.username);
    preferences.putString("password", mqttSettings.password);
    preferences.putString("topic", mqttSettings.topic);
    preferences.end();
    Serial.println("✅ MQTT настройки сохранены");
}

void loadMQTTSettings() {
    preferences.begin("mqtt", true);
    mqttSettings.enabled = preferences.getBool("enabled", false);
    strcpy(mqttSettings.server, preferences.getString("server", "192.168.1.100").c_str());
    mqttSettings.port = preferences.getInt("port", 1883);
    strcpy(mqttSettings.username, preferences.getString("username", "").c_str());
    strcpy(mqttSettings.password, preferences.getString("password", "").c_str());
    strcpy(mqttSettings.topic, preferences.getString("topic", "scd41/sensors").c_str());
    preferences.end();
}

void connectMQTT() {
    if (!mqttSettings.enabled || isAPMode) return;
    
    mqttClient.setServer(mqttSettings.server, mqttSettings.port);
    String clientId = "ESP32-SCD41-" + WiFi.macAddress();
    bool connected = (strlen(mqttSettings.username) > 0) ? 
        mqttClient.connect(clientId.c_str(), mqttSettings.username, mqttSettings.password) :
        mqttClient.connect(clientId.c_str());
    
    if(connected) {
        Serial.println("✅ MQTT подключен");
    } else {
        Serial.printf("❌ MQTT ошибка: %d\n", mqttClient.state());
    }
}

void publishMQTTData() {
    if(!mqttSettings.enabled || !mqttClient.connected() || isAPMode) {
        return;
    }
    
    JsonDocument doc;
    doc["co2"] = co2;
    doc["temperature"] = temperature;
    doc["humidity"] = humidity;
    doc["timestamp"] = millis();
    
    String payload;
    serializeJson(doc, payload);
    
    if(mqttClient.publish(mqttSettings.topic, payload.c_str())) {
        Serial.println("📨 MQTT данные отправлены");
    }
    mqttClient.loop();
}

// Вспомогательные функции калибровки.
// Библиотека выполняет команды калибровки/конфигурации (FRC, ASC, серийник, factory reset)
// только когда периодические измерения остановлены — иначе молча возвращает ошибку.
// Поэтому перед любой такой командой временно останавливаем измерения.
bool beginSensorConfig() {
    if(!sensorConnected) return false;
    return scd4x.stopPeriodicMeasurement(); // внутри ждёт 500 мс, как требует даташит
}

void endSensorConfig() {
    if(sensorConnected) scd4x.startPeriodicMeasurement();
}

// Настройки датчика храним и в Preferences: ASC нужно передавать в begin(),
// иначе она принудительно включается при каждой загрузке
void saveASCSetting(bool enabled) {
    preferences.begin("sensor", false);
    preferences.putBool("asc", enabled);
    preferences.end();
}

bool loadASCSetting() {
    preferences.begin("sensor", true);
    bool enabled = preferences.getBool("asc", true);
    preferences.end();
    return enabled;
}

void setupWebServer() {
    // Главная страница
    server.on("/", []() {
        server.send(200, "text/html", index_html);
    });
    
    // API данных
    server.on("/api/data", []() {
        JsonDocument doc;
        doc["co2"] = co2;
        doc["temp"] = temperature;
        doc["humidity"] = humidity;
        doc["connected"] = sensorConnected;
        doc["mqtt_enabled"] = mqttSettings.enabled;
        doc["mqtt_connected"] = mqttClient.connected();
        doc["ap_mode"] = isAPMode;
        
        if(isAPMode) {
            doc["ap_ssid"] = AP_SSID;
        } else {
            doc["wifi_ssid"] = WiFi.SSID();
        }
        
        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
    });
    
    // Wi-Fi информация
    server.on("/api/wifi/info", []() {
        JsonDocument doc;
        doc["ap_mode"] = isAPMode;
        
        if(isAPMode) {
            doc["ap_ssid"] = AP_SSID;
            doc["ip"] = WiFi.softAPIP().toString();
            doc["stations"] = WiFi.softAPgetStationNum();
        } else {
            doc["ssid"] = WiFi.SSID();
            doc["ip"] = WiFi.localIP().toString();
            doc["rssi"] = WiFi.RSSI();
        }
        
        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
    });
    
    // Сохранение Wi-Fi настроек
    server.on("/api/wifi/config", HTTP_POST, []() {
        if(server.hasArg("plain")) {
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, server.arg("plain"));
            
            if(!error) {
                String ssid = doc["ssid"].as<String>();
                String password = doc["password"].as<String>();
                
                if(ssid.length() > 0) {
                    saveWiFiSettings(ssid, password);
                    server.send(200, "application/json", "{\"success\":true}");
                    delay(1000);
                    ESP.restart();
                } else {
                    server.send(400, "application/json", "{\"success\":false,\"message\":\"SSID required\"}");
                }
            } else {
                server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
            }
        }
    });
    
    // MQTT конфиг GET
    server.on("/api/mqtt/config", HTTP_GET, []() {
        JsonDocument doc;
        doc["enabled"] = mqttSettings.enabled;
        doc["server"] = mqttSettings.server;
        doc["port"] = mqttSettings.port;
        doc["username"] = mqttSettings.username;
        doc["topic"] = mqttSettings.topic;
        
        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
    });
    
    // MQTT конфиг POST
    server.on("/api/mqtt/config", HTTP_POST, []() {
        if(server.hasArg("plain")) {
            JsonDocument doc;
            DeserializationError error = deserializeJson(doc, server.arg("plain"));
            
            if(!error) {
                mqttSettings.enabled = doc["enabled"];
                strcpy(mqttSettings.server, doc["server"]);
                mqttSettings.port = doc["port"];
                strcpy(mqttSettings.username, doc["username"]);
                strcpy(mqttSettings.password, doc["password"]);
                strcpy(mqttSettings.topic, doc["topic"]);
                
                saveMQTTSettings();
                server.send(200, "application/json", "{\"success\":true}");
                delay(1000);
                ESP.restart();
            } else {
                server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
            }
        }
    });
    
    // Информация о датчика (команды чтения доступны только с остановленными измерениями)
    server.on("/api/sensor/info", []() {
        JsonDocument doc;
        doc["connected"] = sensorConnected;

        if(sensorConnected && beginSensorConfig()) {
            doc["asc_enabled"] = scd4x.getAutomaticSelfCalibrationEnabled();
            char serialNumber[13];
            if(scd4x.getSerialNumber(serialNumber)) {
                doc["serial_number"] = serialNumber;
            } else {
                doc["serial_number"] = "N/A";
            }
            endSensorConfig();
        } else {
            doc["asc_enabled"] = false;
            doc["serial_number"] = "N/A";
        }
        
        String response;
        serializeJson(doc, response);
        server.send(200, "application/json", response);
    });
    
    // Установка ASC
    server.on("/api/calibration/asc", HTTP_POST, []() {
        if(!sensorConnected) {
            server.send(400, "application/json", "{\"success\":false,\"message\":\"Датчик не подключен\"}");
            return;
        }
        if(!server.hasArg("plain")) {
            server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid request\"}");
            return;
        }
        JsonDocument doc;
        if(deserializeJson(doc, server.arg("plain"))) {
            server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
            return;
        }
        bool enabled = doc["enabled"] | false;

        if(!beginSensorConfig()) {
            server.send(500, "application/json", "{\"success\":false,\"message\":\"Не удалось остановить измерения\"}");
            return;
        }

        // persistSettings: без этого настройка ASC теряется при перезагрузке
        bool ok = scd4x.setAutomaticSelfCalibrationEnabled(enabled) && scd4x.persistSettings();
        if(ok) {
            ascSetting = enabled;
            saveASCSetting(enabled);
        }
        endSensorConfig();

        if(ok) {
            server.send(200, "application/json", "{\"success\":true,\"message\":\"ASC " + String(enabled?"включена":"выключена") + "\"}");
        } else {
            server.send(500, "application/json", "{\"success\":false,\"message\":\"Ошибка установки ASC\"}");
        }
    });
    
    // Калибровка свежим воздухом
    server.on("/api/calibration/fresh_air", HTTP_POST, []() {
        if(!sensorConnected) {
            server.send(500, "application/json", "{\"success\":false,\"message\":\"Датчик не подключен\"}");
            return;
        }
        if(!beginSensorConfig()) {
            server.send(500, "application/json", "{\"success\":false,\"message\":\"Не удалось остановить измерения\"}");
            return;
        }

        float correction = 0;
        // Булев перегруз: 0.0 означает и «ошибку», и «успех без коррекции»
        bool ok = scd4x.performForcedRecalibration(400, &correction);
        endSensorConfig();

        if(ok) {
            server.send(200, "application/json", "{\"success\":true,\"message\":\"Калибровка на 400 ppm выполнена, коррекция " + String((int)correction) + " ppm\"}");
        } else {
            server.send(500, "application/json", "{\"success\":false,\"message\":\"Ошибка калибровки (датчик должен работать >3 мин в свежем воздухе)\"}");
        }
    });
    
    // Принудительная калибровка
    server.on("/api/calibration/force", HTTP_POST, []() {
        if(!sensorConnected) {
            server.send(500, "application/json", "{\"success\":false,\"message\":\"Датчик не подключен\"}");
            return;
        }
        if(!server.hasArg("plain")) {
            server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid request\"}");
            return;
        }
        JsonDocument doc;
        if(deserializeJson(doc, server.arg("plain"))) {
            server.send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON\"}");
            return;
        }
        int target = doc["value"] | 0;

        if(target < 400 || target > 2000) {
            server.send(400, "application/json", "{\"success\":false,\"message\":\"Значение должно быть 400-2000 ppm\"}");
            return;
        }
        if(!beginSensorConfig()) {
            server.send(500, "application/json", "{\"success\":false,\"message\":\"Не удалось остановить измерения\"}");
            return;
        }

        float correction = 0;
        bool ok = scd4x.performForcedRecalibration((uint16_t)target, &correction);
        endSensorConfig();

        if(ok) {
            server.send(200, "application/json", "{\"success\":true,\"message\":\"Калибровка на "+String(target)+" ppm выполнена, коррекция " + String((int)correction) + " ppm\"}");
        } else {
            server.send(500, "application/json", "{\"success\":false,\"message\":\"Ошибка калибровки\"}");
        }
    });
    
    // Сброс к заводским
    server.on("/api/calibration/factory_reset", HTTP_POST, []() {
        if(!sensorConnected) {
            server.send(500, "application/json", "{\"success\":false,\"message\":\"Датчик не подключен\"}");
            return;
        }
        if(!beginSensorConfig()) {
            server.send(500, "application/json", "{\"success\":false,\"message\":\"Не удалось остановить измерения\"}");
            return;
        }

        bool ok = scd4x.performFactoryReset();
        if(ok) {
            // После сброса ASC возвращается к значению по умолчанию — читаем его и запоминаем
            ascSetting = scd4x.getAutomaticSelfCalibrationEnabled();
            saveASCSetting(ascSetting);
        }
        endSensorConfig();

        if(ok) {
            server.send(200, "application/json", "{\"success\":true,\"message\":\"Сброс выполнен\"}");
        } else {
            server.send(500, "application/json", "{\"success\":false,\"message\":\"Ошибка сброса\"}");
        }
    });
    
    // Перезапуск датчика
    server.on("/api/sensor/restart", HTTP_POST, []() {
        if(sensorConnected) {
            scd4x.stopPeriodicMeasurement();
            delay(100);
            if(scd4x.startPeriodicMeasurement()) {
                server.send(200, "application/json", "{\"success\":true,\"message\":\"Датчик перезапущен\"}");
            } else {
                server.send(500, "application/json", "{\"success\":false,\"message\":\"Ошибка перезапуска\"}");
            }
        } else {
            server.send(500, "application/json", "{\"success\":false,\"message\":\"Датчик не подключен\"}");
        }
    });
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n\n╔═══════════════════════════════════════╗");
    Serial.println("║     SCD41 Web + MQTT Монитор       ║");
    Serial.println("╚═══════════════════════════════════════╝\n");
    
    // Загрузка настроек
    loadMQTTSettings();
    
    // Инициализация I2C
    Wire.begin(I2C_SDA, I2C_SCL);
    Serial.println("I2C инициализирован (SDA=8, SCL=9)");
    
    // Инициализация датчика
    // measBegin=false: измерения запускаем сами, после чтения ASC
    // autoCalibrate = сохранённое значение ASC (по умолчанию begin() всегда включает ASC)
    ascSetting = loadASCSetting();
    Serial.println("\nПоиск SCD41 датчика...");
    if(!scd4x.begin(Wire, false, ascSetting)) {
        Serial.println("❌ SCD41 не обнаружен!");
        Serial.println("   Проверьте подключение:");
        Serial.println("   - SDA -> GPIO8");
        Serial.println("   - SCL -> GPIO9");
        Serial.println("   - VCC -> 3.3V");
        Serial.println("   - GND -> GND");
        sensorConnected = false;
    } else {
        Serial.println("✅ SCD41 найден!");
        // Измерения ещё не запущены, поэтому запрос ASC вернёт реальное значение
        Serial.printf("   ASC статус: %s\n",
            scd4x.getAutomaticSelfCalibrationEnabled() ? "Включена" : "Выключена");

        if(scd4x.startPeriodicMeasurement()) {
            Serial.println("✅ Периодические измерения запущены");
            sensorConnected = true;
        } else {
            Serial.println("❌ Ошибка запуска измерений");
            sensorConnected = false;
        }
    }
    
    // Подключение к Wi-Fi (если есть сохраненные настройки)
    Serial.println("\n📡 Настройка Wi-Fi...");
    if(!connectToWiFi()) {
        startAccessPoint();
    }
    
    // Запуск веб-сервера
    setupWebServer();
    server.begin();
    Serial.println("\n✅ Веб-сервер запущен!");
    
    if(isAPMode) {
        Serial.println("\n🔴 РЕЖИМ ТОЧКИ ДОСТУПА");
        Serial.println("═══════════════════════════════════");
        Serial.printf("📡 SSID: %s\n", AP_SSID);
        Serial.printf("🔑 Пароль: %s\n", AP_PASSWORD);
        Serial.printf("🌐 IP адрес: http://%s\n", WiFi.softAPIP().toString().c_str());
        Serial.println("\n⚙️ ДАЛЬНЕЙШАЯ НАСТРОЙКА:");
        Serial.println("   1. Подключитесь к Wi-Fi сети SCD41-Monitor");
        Serial.println("   2. Откройте браузер и перейдите по IP адресу выше");
        Serial.println("   3. Перейдите на вкладку 'Wi-Fi'");
        Serial.println("   4. Введите данные вашей домашней Wi-Fi сети");
        Serial.println("   5. Нажмите 'Сохранить' - ESP32 перезагрузится");
    } else {
        Serial.println("\n🟢 РЕЖИМ КЛИЕНТА");
        Serial.println("═══════════════════════════════════");
        Serial.printf("🌐 Откройте в браузере: http://%s\n", WiFi.localIP().toString());
        
        if(mqttSettings.enabled) {
            connectMQTT();
        }
    }
    
    Serial.println("\n✅ Готов к работе!\n");
}

void loop() {
    server.handleClient();
    mqttClient.loop();
    
    // Чтение данных с датчика
    if(sensorConnected && millis() - lastSensorRead >= sensorReadInterval) {
        lastSensorRead = millis();
        
        if(scd4x.readMeasurement()) {
            co2 = scd4x.getCO2();
            temperature = scd4x.getTemperature();
            humidity = scd4x.getHumidity();
            
            // Вывод в Serial (редко, чтобы не забивать)
            static unsigned long lastPrint = 0;
            if(millis() - lastPrint > 5000) {
                lastPrint = millis();
                Serial.printf("📊 CO₂: %.0f ppm | 🌡️ %.1f°C | 💧 %.1f%%\n", 
                    co2, temperature, humidity);
            }
            
            // Публикация в MQTT
            static unsigned long lastMQTT = 0;
            if(!isAPMode && mqttSettings.enabled && millis() - lastMQTT >= mqttPublishInterval) {
                lastMQTT = millis();
                publishMQTTData();
            }
        }
    }
    
    delay(100);
}