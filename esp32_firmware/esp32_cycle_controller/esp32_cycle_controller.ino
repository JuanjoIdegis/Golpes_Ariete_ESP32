/*
 * ESP32 Cycle Controller - Conexión AWS Cloud (Multi-Dispositivo por Planta)
 * Incluye Wi-Fi Manager inteligente (Portal Cautivo de Configuración de Wi-Fi)
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include "config.h"

enum SystemState {
    STATE_STOPPED = 0,
    STATE_ON = 1,
    STATE_OFF = 2,
    STATE_NO_LEVEL = 3
};

// Configuración de Identificador de Dispositivo por Planta
String deviceId = "esp32_01"; 

SystemState currentState = STATE_STOPPED;
unsigned long timeOnSec = 5;      
unsigned long timeOffSec = 5;     
unsigned long cycleCount = 0;     
bool isRunning = false;           
bool hasLevel = true;             
bool useLevelSensor = false;      
unsigned long syncIntervalMs = AWS_SYNC_INTERVAL_MS;

unsigned long lastStateChangeMs = 0;
unsigned long lastAwsSyncMs = 0;

Preferences preferences;
WebServer server(80);
bool apPortalActive = false;

void initHardware();
void setRelayState(bool turnOn);
bool checkLevelSensor();
void loadSettingsFromNVS();
void saveCycleCountToNVS();
void saveSettingsToNVS();
void handleCycleStateMachine();
void syncWithAWS();
void parseAWSResponse(String payload);
void startWiFiPortal();
void performHTTPUpdate(String otaUrl);

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n--- ESP32 Cycle Controller (AWS Cloud Client) ---");

    initHardware();
    loadSettingsFromNVS();

    // Intentar conectar a la Wi-Fi guardada o por defecto
    preferences.begin("wifi_config", true);
    String storedSsid = preferences.getString("ssid", WIFI_SSID);
    String storedPass = preferences.getString("pass", WIFI_PASSWORD);
    preferences.end();

    // Iniciar siempre Punto de Acceso (AP) para poder reconfigurar Wi-Fi en cualquier momento
    WiFi.mode(WIFI_AP_STA);
    String apName = "Config-WiFi-" + deviceId;
    WiFi.softAP(apName.c_str(), "12345678");

    WiFi.begin(storedSsid.c_str(), storedPass.c_str());
    Serial.print("Conectando a Wi-Fi: ");
    Serial.println(storedSsid);
    Serial.printf("Punto de acceso propio activado: %s (IP AP: %s)\n", apName.c_str(), WiFi.softAPIP().toString().c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 15) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[Wi-Fi] Conectado exitosamente!");
        Serial.print("[Wi-Fi] IP Local: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\n[Wi-Fi] No se pudo conectar a la red router. El Portal Cautivo sigue disponible.");
    }

    startWiFiPortal();
    lastStateChangeMs = millis();
}

void loop() {
    if (apPortalActive) {
        server.handleClient();
    }
    
    hasLevel = checkLevelSensor();
    handleCycleStateMachine();

    if (millis() - lastAwsSyncMs >= syncIntervalMs) {
        lastAwsSyncMs = millis();
        if (WiFi.status() == WL_CONNECTED) {
            syncWithAWS();
        }
    }
}

void startWiFiPortal() {
    apPortalActive = true;
    WiFi.mode(WIFI_AP_STA);
    String apName = "Config-WiFi-" + deviceId;
    WiFi.softAP(apName.c_str(), "12345678");

    Serial.println("=======================================================");
    Serial.printf(" Portal de Configuración Wi-Fi Activo: %s\n", apName.c_str());
    Serial.printf(" Entra a: http://%s para cambiar la red Wi-Fi\n", WiFi.softAPIP().toString().c_str());
    Serial.println("=======================================================");

    server.on("/", HTTP_GET, []() {
        String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="UTF-8"><title>Configurar Wi-Fi ESP32</title>
<script src="https://cdn.tailwindcss.com"></script></head>
<body class="bg-slate-900 text-white p-6 font-sans max-w-md mx-auto">
<h2 class="text-xl font-bold mb-4 text-blue-400">Configurar Red Wi-Fi ESP32</h2>
<form action="/save_wifi" method="POST" class="space-y-4">
<div><label class="block text-sm">Nombre Wi-Fi (SSID)</label>
<input type="text" name="ssid" required class="w-full bg-slate-800 p-3 rounded border border-slate-700 font-bold"></div>
<div><label class="block text-sm">Contraseña</label>
<input type="password" name="pass" class="w-full bg-slate-800 p-3 rounded border border-slate-700 font-bold"></div>
<button type="submit" class="w-full py-3 bg-blue-600 hover:bg-blue-500 font-bold rounded-xl">Guardar y Reiniciar</button>
</form></body></html>
)rawliteral";
        server.send(200, "text/html", html);
    });

    server.on("/save_wifi", HTTP_POST, []() {
        String newSsid = server.arg("ssid");
        String newPass = server.arg("pass");

        preferences.begin("wifi_config", false);
        preferences.putString("ssid", newSsid);
        preferences.putString("pass", newPass);
        preferences.end();

        server.send(200, "text/html", "<h2>Wi-Fi Guardado! Reiniciando ESP32...</h2>");
        delay(2000);
        ESP.restart();
    });

    server.begin();
}

void initHardware() {
    pinMode(RELAY_PIN, OUTPUT);
    setRelayState(false);
    pinMode(LEVEL_SENSOR_PIN, INPUT_PULLDOWN);
    hasLevel = checkLevelSensor();
}

void setRelayState(bool turnOn) {
    if (RELAY_ACTIVE_HIGH) {
        digitalWrite(RELAY_PIN, turnOn ? HIGH : LOW);
    } else {
        digitalWrite(RELAY_PIN, turnOn ? LOW : HIGH);
    }
}

bool checkLevelSensor() {
    int pinVal = digitalRead(LEVEL_SENSOR_PIN);
    return (LEVEL_SENSOR_ACTIVE_HIGH) ? (pinVal == HIGH) : (pinVal == LOW);
}

void loadSettingsFromNVS() {
    preferences.begin("cycle_ctrl", false);
    timeOnSec = preferences.getULong("time_on", 5);
    timeOffSec = preferences.getULong("time_off", 5);
    cycleCount = preferences.getULong("cycles", 0);
    isRunning = preferences.getBool("running", false);
    useLevelSensor = preferences.getBool("use_sensor", false);
    deviceId = preferences.getString("dev_id", "esp32_01");
    preferences.end();

    currentState = isRunning ? STATE_ON : STATE_STOPPED;
}

void saveCycleCountToNVS() {
    preferences.begin("cycle_ctrl", false);
    preferences.putULong("cycles", cycleCount);
    preferences.end();
}

void saveSettingsToNVS() {
    preferences.begin("cycle_ctrl", false);
    preferences.putULong("time_on", timeOnSec);
    preferences.putULong("time_off", timeOffSec);
    preferences.putBool("running", isRunning);
    preferences.putBool("use_sensor", useLevelSensor);
    preferences.end();
}

void handleCycleStateMachine() {
    if (useLevelSensor && !hasLevel) {
        if (currentState != STATE_NO_LEVEL) {
            currentState = STATE_NO_LEVEL;
            setRelayState(false);
            Serial.println("[ALERTA NIVEL] Sensor XKC-Y25 sin nivel! Parando relé en OFF.");
        }
        return;
    }

    if (!isRunning) {
        if (currentState != STATE_STOPPED) {
            currentState = STATE_STOPPED;
            setRelayState(false);
            Serial.println("[Estado] Sistema DETENIDO por usuario.");
        }
        return;
    }

    unsigned long currentMs = millis();
    unsigned long elapsedMs = currentMs - lastStateChangeMs;

    switch (currentState) {
        case STATE_STOPPED:
        case STATE_NO_LEVEL:
            currentState = STATE_ON;
            lastStateChangeMs = currentMs;
            setRelayState(true);
            Serial.println("[Estado] Reanudando ciclo -> RELÉ ENCENDIDO (ON)");
            break;

        case STATE_ON:
            setRelayState(true);
            if (elapsedMs >= (timeOnSec * 1000UL)) {
                currentState = STATE_OFF;
                lastStateChangeMs = currentMs;
                setRelayState(false);
                Serial.println("[Estado] Tiempo ON completado -> RELÉ APAGADO (OFF)");
            }
            break;

        case STATE_OFF:
            setRelayState(false);
            if (elapsedMs >= (timeOffSec * 1000UL)) {
                cycleCount++;
                saveCycleCountToNVS();
                Serial.printf("[Estado] Ciclo completado! Total de ciclos: %lu\n", cycleCount);

                currentState = STATE_ON;
                lastStateChangeMs = currentMs;
                setRelayState(true);
                Serial.println("[Estado] Reiniciando ciclo -> RELÉ ENCENDIDO (ON)");
            }
            break;
    }
}

void syncWithAWS() {
    WiFiClientSecure client;
    client.setInsecure(); // Permitir handshake SSL HTTPS con AWS API Gateway
    HTTPClient http;
    String url = String(AWS_API_ENDPOINT) + "?api_key=" + String(AWS_API_KEY);
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("x-api-key", AWS_API_KEY);

    unsigned long remainingSec = 0;
    unsigned long elapsedSec = (millis() - lastStateChangeMs) / 1000UL;

    if (currentState == STATE_ON) {
        remainingSec = (elapsedSec < timeOnSec) ? (timeOnSec - elapsedSec) : 0;
    } else if (currentState == STATE_OFF) {
        remainingSec = (elapsedSec < timeOffSec) ? (timeOffSec - elapsedSec) : 0;
    }

    String stateStr = "STOPPED";
    if (currentState == STATE_ON) stateStr = "ON";
    else if (currentState == STATE_OFF) stateStr = "OFF";
    else if (currentState == STATE_NO_LEVEL) stateStr = "NO_LEVEL";

    StaticJsonDocument<256> doc;
    doc["client_type"] = "esp32";
    doc["device_id"] = deviceId;
    doc["state"] = stateStr;
    doc["is_running"] = isRunning;
    doc["has_level"] = hasLevel;
    doc["use_sensor"] = useLevelSensor;
    doc["time_on"] = timeOnSec;
    doc["time_off"] = timeOffSec;
    doc["cycle_count"] = cycleCount;
    doc["remaining_sec"] = remainingSec;

    String requestBody;
    serializeJson(doc, requestBody);

    Serial.println("[AWS] Enviando telemetría...");
    int httpResponseCode = http.POST(requestBody);

    if (httpResponseCode > 0) {
        String response = http.getString();
        Serial.printf("[AWS OK] Respuesta recibida (Código %d): %s\n", httpResponseCode, response.c_str());
        if (httpResponseCode == 200) {
            parseAWSResponse(response);
        }
    } else {
        Serial.printf("[AWS Error] Petición fallida: %s (código: %d)\n", http.errorToString(httpResponseCode).c_str(), httpResponseCode);
    }

    http.end();
}

void parseAWSResponse(String payload) {
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error) return;

    bool updated = false;

    if (doc.containsKey("cmd_reset_cycles") && doc["cmd_reset_cycles"].as<bool>() == true) {
        cycleCount = 0;
        saveCycleCountToNVS();
        Serial.println("[AWS Cmd] Contador de ciclos reseteado a 0.");
    }

    if (doc.containsKey("use_sensor")) {
        bool newSensorSetting = doc["use_sensor"].as<bool>();
        if (newSensorSetting != useLevelSensor) {
            useLevelSensor = newSensorSetting;
            updated = true;
        }
    }

    if (doc.containsKey("target_time_on")) {
        unsigned long newOn = doc["target_time_on"].as<unsigned long>();
        if (newOn != timeOnSec && newOn > 0) {
            timeOnSec = newOn;
            updated = true;
        }
    }

    if (doc.containsKey("target_time_off")) {
        unsigned long newOff = doc["target_time_off"].as<unsigned long>();
        if (newOff != timeOffSec && newOff > 0) {
            timeOffSec = newOff;
            updated = true;
        }
    }

    if (doc.containsKey("target_running")) {
        bool newRunning = doc["target_running"].as<bool>();
        if (newRunning != isRunning) {
            isRunning = newRunning;
            updated = true;
            if (isRunning) {
                currentState = (useLevelSensor && !hasLevel) ? STATE_NO_LEVEL : STATE_ON;
                lastStateChangeMs = millis();
            } else {
                currentState = STATE_STOPPED;
                setRelayState(false);
            }
        }
    }

    if (doc.containsKey("sync_interval_ms")) {
        unsigned long newSync = doc["sync_interval_ms"].as<unsigned long>();
        if (newSync >= 1000 && newSync <= 60000) {
            syncIntervalMs = newSync;
        }
    }

    if (doc.containsKey("cmd_ota_update") && doc["cmd_ota_update"].as<bool>() == true) {
        String otaUrl = doc["ota_url"].as<String>();
        if (otaUrl.length() > 5) {
            Serial.printf("[OTA] Recibida orden de actualización desde: %s\n", otaUrl.c_str());
            performHTTPUpdate(otaUrl);
        }
    }

    if (updated) {
        saveSettingsToNVS();
    }
}

void performHTTPUpdate(String otaUrl) {
    WiFiClientSecure client;
    client.setInsecure(); // Permitir descargar binario OTA desde HTTPS

    Serial.println("[OTA] Iniciando actualización de firmware por Wi-Fi...");
    t_httpUpdate_return ret = httpUpdate.update(client, otaUrl);

    switch (ret) {
        case HTTP_UPDATE_FAILED:
            Serial.printf("[OTA] Error de actualización (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
            break;
        case HTTP_UPDATE_NO_UPDATES:
            Serial.println("[OTA] No hay nuevas actualizaciones disponibles.");
            break;
        case HTTP_UPDATE_OK:
            Serial.println("[OTA] ¡Firmware actualizado exitosamente! Reiniciando ESP32...");
            ESP.restart();
            break;
    }
}
