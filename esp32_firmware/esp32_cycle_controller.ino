/*
 * ESP32 Cycle Controller - Conexión Cloud AWS (API Gateway + Lambda + DynamoDB)
 * Incluye autenticación x-api-key (Wiz Audit Compliance) y sensor de nivel opcional.
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "config.h"

enum SystemState {
    STATE_STOPPED = 0,
    STATE_ON = 1,
    STATE_OFF = 2,
    STATE_NO_LEVEL = 3
};

// Variables globales de control
SystemState currentState = STATE_STOPPED;
unsigned long timeOnSec = 5;      // Tiempo encendido por defecto (segundos)
unsigned long timeOffSec = 5;     // Tiempo apagado por defecto (segundos)
unsigned long cycleCount = 0;     // Contador total de ciclos
bool isRunning = false;           // Estado de ejecución
bool hasLevel = true;             // Estado del sensor XKC-Y25-PNP
bool useLevelSensor = false;      // OPCIONAL: Habilitar/Deshabilitar protección por sensor de nivel

// Control del temporizador millis()
unsigned long lastStateChangeMs = 0;
unsigned long lastAwsSyncMs = 0;

Preferences preferences;

// Prototipos de funciones
void initHardware();
void setRelayState(bool turnOn);
bool checkLevelSensor();
void loadSettingsFromNVS();
void saveCycleCountToNVS();
void saveSettingsToNVS();
void handleCycleStateMachine();
void syncWithAWS();
void parseAWSResponse(String payload);

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n--- ESP32 Cycle Controller (Modo AWS Cloud Client) ---");

    initHardware();
    loadSettingsFromNVS();

    // Conexión Wi-Fi
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Conectando a Wi-Fi: ");
    Serial.println(WIFI_SSID);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[Wi-Fi] Conectado exitosamente!");
        Serial.print("[Wi-Fi] IP Local: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\n[Wi-Fi] No se pudo conectar a la red Wi-Fi. Continuando en modo offline...");
    }

    lastStateChangeMs = millis();
}

void loop() {
    hasLevel = checkLevelSensor();
    handleCycleStateMachine();

    // Sincronizar periódicamente con AWS API Gateway
    if (millis() - lastAwsSyncMs >= AWS_SYNC_INTERVAL_MS) {
        lastAwsSyncMs = millis();
        if (WiFi.status() == WL_CONNECTED) {
            syncWithAWS();
        }
    }
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
    preferences.end();

    Serial.println("[NVS] Ajustes cargados:");
    Serial.printf("      T_ON: %lu s | T_OFF: %lu s | Ciclos: %lu | En ejecución: %s | Sensor: %s\n", 
                  timeOnSec, timeOffSec, cycleCount, isRunning ? "SI" : "NO", useLevelSensor ? "ACTIVADO" : "DESACTIVADO");

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
    // PROTECCIÓN DE NIVEL: Evalúa solo si el sensor está activado en los ajustes
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
    HTTPClient http;
    
    // URL con autenticación por querystring + cabecera x-api-key (Seguridad Wiz)
    String url = String(AWS_API_ENDPOINT) + "?api_key=" + String(AWS_API_KEY);
    http.begin(url);
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
    doc["device_id"] = "esp32_01";
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

    int httpResponseCode = http.POST(requestBody);

    if (httpResponseCode > 0) {
        String response = http.getString();
        if (httpResponseCode == 200) {
            parseAWSResponse(response);
        } else {
            Serial.printf("[AWS Cloud HTTP] Respuesta HTTP %d\n", httpResponseCode);
        }
    } else {
        Serial.printf("[AWS HTTP Error] Petición fallida: %s\n", http.errorToString(httpResponseCode).c_str());
    }

    http.end();
}

void parseAWSResponse(String payload) {
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, payload);

    if (error) {
        Serial.print("[JSON Error] Deserialización fallida: ");
        Serial.println(error.c_str());
        return;
    }

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
            Serial.printf("[AWS Config] Sensor de Nivel opcional: %s\n", useLevelSensor ? "ACTIVADO" : "DESACTIVADO");
        }
    }

    if (doc.containsKey("target_time_on")) {
        unsigned long newOn = doc["target_time_on"].as<unsigned long>();
        if (newOn != timeOnSec && newOn > 0) {
            timeOnSec = newOn;
            updated = true;
            Serial.printf("[AWS Config] Nuevo Tiempo ON: %lu s\n", timeOnSec);
        }
    }

    if (doc.containsKey("target_time_off")) {
        unsigned long newOff = doc["target_time_off"].as<unsigned long>();
        if (newOff != timeOffSec && newOff > 0) {
            timeOffSec = newOff;
            updated = true;
            Serial.printf("[AWS Config] Nuevo Tiempo OFF: %lu s\n", timeOffSec);
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
            Serial.printf("[AWS Config] Estado Ejecución modificado: %s\n", isRunning ? "START" : "STOP");
        }
    }

    if (updated) {
        saveSettingsToNVS();
    }
}
