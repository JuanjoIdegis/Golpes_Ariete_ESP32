/*
 * ESP32 Multi-Channel Controller v5.0 - Ariete + Inversión de Polaridad Dual
 * Soporta:
 * 1. Salida Golpe de Ariete (GPIO 2)
 * 2. Canal 1 Inversión de Polaridad (GPIO 4 Relé A, GPIO 16 Relé B)
 * 3. Canal 2 Inversión de Polaridad (GPIO 17 Relé A, GPIO 18 Relé B)
 * 4. Portal Cautivo Wi-Fi & Sincronización AWS Cloud
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "config.h"

// Estados de Inversión de Polaridad
enum PolarityState {
    POL_STOPPED = 0,
    POL_A = 1,          // Relé A (Directa)
    POL_DEADBAND_1 = 2, // Pausa de seguridad A -> B
    POL_B = 3,          // Relé B (Inversa)
    POL_DEADBAND_2 = 4  // Pausa de seguridad B -> A
};

// Estados del Canal Golpe de Ariete
enum ArieteState {
    ARIETE_STOPPED = 0,
    ARIETE_ON = 1,
    ARIETE_OFF = 2,
    ARIETE_NO_LEVEL = 3
};

// Estructura para Canales de Inversión de Polaridad
struct PolarityChannel {
    uint8_t pinA;
    uint8_t pinB;
    bool isRunning;
    PolarityState state;
    unsigned long timeA_sec;    // Tiempo en Polaridad A
    unsigned long timeB_sec;    // Tiempo en Polaridad B
    unsigned long deadband_sec; // Pausa de seguridad entre cambios (Banda Muerta)
    unsigned long lastChangeMs;
    unsigned long cycleCount;

    void init(uint8_t pA, uint8_t pB) {
        pinA = pA;
        pinB = pB;
        pinMode(pinA, OUTPUT);
        pinMode(pinB, OUTPUT);
        setRelays(false, false);
        isRunning = false;
        state = POL_STOPPED;
        timeA_sec = 60;
        timeB_sec = 60;
        deadband_sec = 3;
        lastChangeMs = millis();
        cycleCount = 0;
    }

    void setRelays(bool aOn, bool bOn) {
        // Garantía de Protección de Hardware: NUNCA ambos relés ON simultáneamente
        if (aOn && bOn) {
            digitalWrite(pinA, LOW);
            digitalWrite(pinB, LOW);
            return;
        }
        digitalWrite(pinA, aOn ? HIGH : LOW);
        digitalWrite(pinB, bOn ? HIGH : LOW);
    }

    void update() {
        if (!isRunning) {
            if (state != POL_STOPPED) {
                state = POL_STOPPED;
                setRelays(false, false);
            }
            return;
        }

        unsigned long currentMs = millis();
        unsigned long elapsedMs = currentMs - lastChangeMs;

        switch (state) {
            case POL_STOPPED:
                state = POL_A;
                setRelays(true, false);
                lastChangeMs = currentMs;
                break;

            case POL_A:
                if (elapsedMs >= timeA_sec * 1000UL) {
                    state = POL_DEADBAND_1;
                    setRelays(false, false);
                    lastChangeMs = currentMs;
                }
                break;

            case POL_DEADBAND_1:
                if (elapsedMs >= deadband_sec * 1000UL) {
                    state = POL_B;
                    setRelays(false, true);
                    lastChangeMs = currentMs;
                }
                break;

            case POL_B:
                if (elapsedMs >= timeB_sec * 1000UL) {
                    state = POL_DEADBAND_2;
                    setRelays(false, false);
                    lastChangeMs = currentMs;
                }
                break;

            case POL_DEADBAND_2:
                if (elapsedMs >= deadband_sec * 1000UL) {
                    state = POL_A;
                    setRelays(true, false);
                    cycleCount++;
                    lastChangeMs = currentMs;
                }
                break;
        }
    }

    unsigned long getRemainingSec() {
        if (!isRunning || state == POL_STOPPED) return 0;
        unsigned long elapsedSec = (millis() - lastChangeMs) / 1000UL;
        unsigned long targetSec = 0;
        if (state == POL_A) targetSec = timeA_sec;
        else if (state == POL_DEADBAND_1 || state == POL_DEADBAND_2) targetSec = deadband_sec;
        else if (state == POL_B) targetSec = timeB_sec;

        if (elapsedSec >= targetSec) return 0;
        return targetSec - elapsedSec;
    }

    String getStateString() {
        switch (state) {
            case POL_STOPPED: return "STOPPED";
            case POL_A: return "POLARITY_A";
            case POL_DEADBAND_1: return "DEADBAND";
            case POL_B: return "POLARITY_B";
            case POL_DEADBAND_2: return "DEADBAND";
            default: return "UNKNOWN";
        }
    }
};

// Estructura para Canal Golpe de Ariete
struct ArieteChannel {
    uint8_t pinRelay;
    bool isRunning;
    ArieteState state;
    unsigned long timeOnSec;
    unsigned long timeOffSec;
    unsigned long lastChangeMs;
    unsigned long cycleCount;

    void init(uint8_t pin) {
        pinRelay = pin;
        pinMode(pinRelay, OUTPUT);
        digitalWrite(pinRelay, LOW);
        isRunning = false;
        state = ARIETE_STOPPED;
        timeOnSec = 5;
        timeOffSec = 5;
        lastChangeMs = millis();
        cycleCount = 0;
    }

    void update(bool hasLevel, bool useSensor) {
        if (useSensor && !hasLevel) {
            if (state != ARIETE_NO_LEVEL) {
                state = ARIETE_NO_LEVEL;
                digitalWrite(pinRelay, LOW);
            }
            return;
        }

        if (!isRunning) {
            if (state != ARIETE_STOPPED) {
                state = ARIETE_STOPPED;
                digitalWrite(pinRelay, LOW);
            }
            return;
        }

        unsigned long currentMs = millis();
        unsigned long elapsedMs = currentMs - lastChangeMs;

        switch (state) {
            case ARIETE_STOPPED:
            case ARIETE_NO_LEVEL:
                state = ARIETE_ON;
                digitalWrite(pinRelay, HIGH);
                lastChangeMs = currentMs;
                break;

            case ARIETE_ON:
                if (elapsedMs >= timeOnSec * 1000UL) {
                    state = ARIETE_OFF;
                    digitalWrite(pinRelay, LOW);
                    lastChangeMs = currentMs;
                }
                break;

            case ARIETE_OFF:
                if (elapsedMs >= timeOffSec * 1000UL) {
                    state = ARIETE_ON;
                    digitalWrite(pinRelay, HIGH);
                    cycleCount++;
                    lastChangeMs = currentMs;
                }
                break;
        }
    }

    unsigned long getRemainingSec() {
        if (!isRunning || state == ARIETE_STOPPED || state == ARIETE_NO_LEVEL) return 0;
        unsigned long elapsedSec = (millis() - lastChangeMs) / 1000UL;
        unsigned long targetSec = (state == ARIETE_ON) ? timeOnSec : timeOffSec;
        if (elapsedSec >= targetSec) return 0;
        return targetSec - elapsedSec;
    }

    String getStateString() {
        switch (state) {
            case ARIETE_STOPPED: return "STOPPED";
            case ARIETE_ON: return "ON";
            case ARIETE_OFF: return "OFF";
            case ARIETE_NO_LEVEL: return "NO_LEVEL";
            default: return "UNKNOWN";
        }
    }
};

// Instancias Globales
String deviceId = "esp32_01"; 
String firmwareVer = "v5.0";

ArieteChannel ariete1;
PolarityChannel polarity1;
PolarityChannel polarity2;

bool hasLevel = true;
bool useLevelSensor = false;
unsigned long syncIntervalMs = AWS_SYNC_INTERVAL_MS;
unsigned long lastAwsSyncMs = 0;

Preferences preferences;
WebServer server(80);
DNSServer dnsServer;
bool apPortalActive = false;

void initHardware();
bool checkLevelSensor();
void loadSettingsFromNVS();
void saveSettingsToNVS();
void syncWithAWS();
void parseAWSResponse(String payload);
void startWiFiPortal();
void performHTTPUpdate(String otaUrl);

void setup() {
    Serial.begin(115200);
    delay(500);

    Serial.println("\n=== ESP32 Multi-Channel Controller v5.0 ===");
    Serial.printf("=== Endpoint AWS: %s\n", AWS_API_ENDPOINT);

    initHardware();
    loadSettingsFromNVS();

    // Conexión Wi-Fi
    preferences.begin("wifi_config", true);
    String storedSsid = preferences.getString("ssid", WIFI_SSID);
    String storedPass = preferences.getString("pass", WIFI_PASSWORD);
    preferences.end();

    if (storedSsid.length() == 0) storedSsid = WIFI_SSID;
    if (storedPass.length() == 0) storedPass = WIFI_PASSWORD;

    WiFi.mode(WIFI_AP_STA);
    WiFi.setAutoReconnect(true);
    String apName = "Config-WiFi-" + deviceId;
    WiFi.softAP(apName.c_str(), "12345678");

    WiFi.begin(storedSsid.c_str(), storedPass.c_str());
    Serial.print("Conectando a Wi-Fi: ");
    Serial.println(storedSsid);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\n[Wi-Fi] Conectado exitosamente! IP: " + WiFi.localIP().toString());
    } else {
        Serial.println("\n[Wi-Fi] Modo AP activo para configuración.");
    }

    startWiFiPortal();
}

void loop() {
    if (apPortalActive) {
        dnsServer.processNextRequest();
        server.handleClient();
    }

    hasLevel = checkLevelSensor();

    // Actualizar máquinas de estado independientes
    ariete1.update(hasLevel, useLevelSensor);
    polarity1.update();
    polarity2.update();

    // Sincronización con AWS Cloud
    if (millis() - lastAwsSyncMs >= syncIntervalMs) {
        lastAwsSyncMs = millis();
        if (WiFi.status() == WL_CONNECTED) {
            syncWithAWS();
        } else {
            static unsigned long lastReconnectMs = 0;
            if (millis() - lastReconnectMs >= 10000) {
                lastReconnectMs = millis();
                WiFi.reconnect();
            }
        }
    }
}

void initHardware() {
    ariete1.init(ARIETE_1_RELAY_PIN);
    polarity1.init(POLARITY_1_RELAY_A_PIN, POLARITY_1_RELAY_B_PIN);
    polarity2.init(POLARITY_2_RELAY_A_PIN, POLARITY_2_RELAY_B_PIN);

    pinMode(LEVEL_SENSOR_PIN, INPUT_PULLDOWN);
    hasLevel = checkLevelSensor();
}

bool checkLevelSensor() {
    int pinVal = digitalRead(LEVEL_SENSOR_PIN);
    return (LEVEL_SENSOR_ACTIVE_HIGH) ? (pinVal == HIGH) : (pinVal == LOW);
}

void loadSettingsFromNVS() {
    preferences.begin("multi_ctrl", false);

    // Cargar Ariete 1
    ariete1.timeOnSec = preferences.getULong("a1_ton", 5);
    ariete1.timeOffSec = preferences.getULong("a1_toff", 5);
    ariete1.cycleCount = preferences.getULong("a1_cyc", 0);
    ariete1.isRunning = preferences.getBool("a1_run", false);

    // Cargar Polaridad 1
    polarity1.timeA_sec = preferences.getULong("p1_ta", 25200);   // 7 horas por defecto
    polarity1.timeB_sec = preferences.getULong("p1_tb", 25200);
    polarity1.deadband_sec = preferences.getULong("p1_td", 3);
    polarity1.cycleCount = preferences.getULong("p1_cyc", 0);
    polarity1.isRunning = preferences.getBool("p1_run", false);

    // Cargar Polaridad 2
    polarity2.timeA_sec = preferences.getULong("p2_ta", 60);      // 1 minuto por defecto
    polarity2.timeB_sec = preferences.getULong("p2_tb", 60);
    polarity2.deadband_sec = preferences.getULong("p2_td", 3);
    polarity2.cycleCount = preferences.getULong("p2_cyc", 0);
    polarity2.isRunning = preferences.getBool("p2_run", false);

    useLevelSensor = preferences.getBool("use_sensor", false);

    // Cargar / Generar Device ID único basado en MAC
    String savedId = preferences.getString("dev_id", "");
    if (savedId.length() > 0) {
        deviceId = savedId;
    } else {
        uint8_t mac[6];
        WiFi.macAddress(mac);
        char autoId[32];
        snprintf(autoId, sizeof(autoId), "esp32_%02x%02x", mac[4], mac[5]);
        deviceId = String(autoId);
    }

    preferences.end();
}

void saveSettingsToNVS() {
    preferences.begin("multi_ctrl", false);
    
    preferences.putULong("a1_ton", ariete1.timeOnSec);
    preferences.putULong("a1_toff", ariete1.timeOffSec);
    preferences.putULong("a1_cyc", ariete1.cycleCount);
    preferences.putBool("a1_run", ariete1.isRunning);

    preferences.putULong("p1_ta", polarity1.timeA_sec);
    preferences.putULong("p1_tb", polarity1.timeB_sec);
    preferences.putULong("p1_td", polarity1.deadband_sec);
    preferences.putULong("p1_cyc", polarity1.cycleCount);
    preferences.putBool("p1_run", polarity1.isRunning);

    preferences.putULong("p2_ta", polarity2.timeA_sec);
    preferences.putULong("p2_tb", polarity2.timeB_sec);
    preferences.putULong("p2_td", polarity2.deadband_sec);
    preferences.putULong("p2_cyc", polarity2.cycleCount);
    preferences.putBool("p2_run", polarity2.isRunning);

    preferences.putBool("use_sensor", useLevelSensor);
    preferences.end();
}

void syncWithAWS() {
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    String fullUrl = String(AWS_API_ENDPOINT) + "?api_key=" + String(AWS_API_KEY);

    if (!http.begin(client, fullUrl)) return;

    http.addHeader("Content-Type", "application/json");

    StaticJsonDocument<1024> doc;
    doc["device_id"] = deviceId;
    doc["firmware_ver"] = firmwareVer;
    doc["has_level"] = hasLevel;
    doc["use_sensor"] = useLevelSensor;

    // Ariete 1
    JsonObject a1 = doc.createNestedObject("ariete_1");
    a1["is_running"] = ariete1.isRunning;
    a1["state"] = ariete1.getStateString();
    a1["time_on"] = ariete1.timeOnSec;
    a1["time_off"] = ariete1.timeOffSec;
    a1["cycle_count"] = ariete1.cycleCount;
    a1["remaining_sec"] = ariete1.getRemainingSec();

    // Polaridad 1
    JsonObject p1 = doc.createNestedObject("polarity_1");
    p1["is_running"] = polarity1.isRunning;
    p1["state"] = polarity1.getStateString();
    p1["time_a"] = polarity1.timeA_sec;
    p1["time_dead"] = polarity1.deadband_sec;
    p1["time_b"] = polarity1.timeB_sec;
    p1["cycle_count"] = polarity1.cycleCount;
    p1["remaining_sec"] = polarity1.getRemainingSec();

    // Polaridad 2
    JsonObject p2 = doc.createNestedObject("polarity_2");
    p2["is_running"] = polarity2.isRunning;
    p2["state"] = polarity2.getStateString();
    p2["time_a"] = polarity2.timeA_sec;
    p2["time_dead"] = polarity2.deadband_sec;
    p2["time_b"] = polarity2.timeB_sec;
    p2["cycle_count"] = polarity2.cycleCount;
    p2["remaining_sec"] = polarity2.getRemainingSec();

    String jsonOutput;
    serializeJson(doc, jsonOutput);

    int httpCode = http.POST(jsonOutput);
    if (httpCode > 0) {
        String payload = http.getString();
        if (httpCode == 200) {
            parseAWSResponse(payload);
        }
    }
    http.end();
}

void parseAWSResponse(String payload) {
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, payload);
    if (error) return;

    bool needsSave = false;

    // Configuración Ariete 1
    if (doc.containsKey("cmd_ariete_1")) {
        JsonObject a1Cmd = doc["cmd_ariete_1"];
        if (a1Cmd.containsKey("is_running")) ariete1.isRunning = a1Cmd["is_running"];
        if (a1Cmd.containsKey("time_on")) ariete1.timeOnSec = a1Cmd["time_on"];
        if (a1Cmd.containsKey("time_off")) ariete1.timeOffSec = a1Cmd["time_off"];
        if (a1Cmd.containsKey("reset_cycles") && a1Cmd["reset_cycles"].as<bool>()) ariete1.cycleCount = 0;
        needsSave = true;
    }

    // Configuración Polaridad 1
    if (doc.containsKey("cmd_polarity_1")) {
        JsonObject p1Cmd = doc["cmd_polarity_1"];
        if (p1Cmd.containsKey("is_running")) polarity1.isRunning = p1Cmd["is_running"];
        if (p1Cmd.containsKey("time_a")) polarity1.timeA_sec = p1Cmd["time_a"];
        if (p1Cmd.containsKey("time_dead")) polarity1.deadband_sec = p1Cmd["time_dead"];
        if (p1Cmd.containsKey("time_b")) polarity1.timeB_sec = p1Cmd["time_b"];
        if (p1Cmd.containsKey("reset_cycles") && p1Cmd["reset_cycles"].as<bool>()) polarity1.cycleCount = 0;
        needsSave = true;
    }

    // Configuración Polaridad 2
    if (doc.containsKey("cmd_polarity_2")) {
        JsonObject p2Cmd = doc["cmd_polarity_2"];
        if (p2Cmd.containsKey("is_running")) polarity2.isRunning = p2Cmd["is_running"];
        if (p2Cmd.containsKey("time_a")) polarity2.timeA_sec = p2Cmd["time_a"];
        if (p2Cmd.containsKey("time_dead")) polarity2.deadband_sec = p2Cmd["time_dead"];
        if (p2Cmd.containsKey("time_b")) polarity2.timeB_sec = p2Cmd["time_b"];
        if (p2Cmd.containsKey("reset_cycles") && p2Cmd["reset_cycles"].as<bool>()) polarity2.cycleCount = 0;
        needsSave = true;
    }

    if (doc.containsKey("use_sensor")) {
        useLevelSensor = doc["use_sensor"];
        needsSave = true;
    }

    if (needsSave) {
        saveSettingsToNVS();
    }

    // Orden de actualización OTA por aire
    if (doc.containsKey("cmd_ota_update") && doc["cmd_ota_update"].as<bool>()) {
        if (doc.containsKey("ota_url")) {
            String url = doc["ota_url"].as<String>();
            if (url.length() > 0) {
                performHTTPUpdate(url);
            }
        }
    }
}

void startWiFiPortal() {
    apPortalActive = true;
    String apName = "Config-WiFi-" + deviceId;
    dnsServer.start(53, "*", WiFi.softAPIP());

    server.on("/", HTTP_GET, []() {
        String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><title>Configurar Wi-Fi ESP32</title>"
                      "<script src='https://cdn.tailwindcss.com'></script></head>"
                      "<body class='bg-slate-900 text-white p-6 max-w-md mx-auto'>"
                      "<h2 class='text-xl font-bold mb-4 text-blue-400'>Configuración ESP32 v5.0 Multi-Canal</h2>"
                      "<form action='/save_wifi' method='POST' class='space-y-4'>"
                      "<div><label>Nombre Wi-Fi (SSID)</label><input type='text' name='ssid' required class='w-full bg-slate-800 p-3 rounded'></div>"
                      "<div><label>Contraseña</label><input type='password' name='pass' class='w-full bg-slate-800 p-3 rounded'></div>"
                      "<button type='submit' class='w-full py-3 bg-blue-600 font-bold rounded-xl'>Guardar y Reiniciar</button>"
                      "</form></body></html>";
        server.send(200, "text/html", html);
    });

    server.on("/save_wifi", HTTP_POST, []() {
        String newSsid = server.arg("ssid");
        String newPass = server.arg("pass");
        preferences.begin("wifi_config", false);
        preferences.putString("ssid", newSsid);
        preferences.putString("pass", newPass);
        preferences.end();
        server.send(200, "text/html", "<h2>Wi-Fi Guardado! Reiniciando...</h2>");
        delay(2000);
        ESP.restart();
    });

    server.begin();
}

void performHTTPUpdate(String otaUrl) {
    WiFiClientSecure client;
    client.setInsecure();

    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    if (!http.begin(client, otaUrl)) return;

    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
        int contentLength = http.getSize();
        if (contentLength > 0 && Update.begin(contentLength)) {
            WiFiClient* stream = http.getStreamPtr();
            size_t written = Update.writeStream(*stream);
            if (written == contentLength && Update.end(true)) {
                http.end();
                delay(1000);
                ESP.restart();
            }
        }
    }
    http.end();
}
