/*
 * ESP32 Multi-Channel Controller v5.1 - Ariete + Inversión de Polaridad Dual con Horas Acumuladas
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

enum PolarityState {
    POL_STOPPED = 0,
    POL_A = 1,          // Relé A (Directa)
    POL_DEADBAND_1 = 2, // Pausa A -> B
    POL_B = 3,          // Relé B (Inversa)
    POL_DEADBAND_2 = 4  // Pausa B -> A
};

enum ArieteState {
    ARIETE_STOPPED = 0,
    ARIETE_ON = 1,
    ARIETE_OFF = 2,
    ARIETE_NO_LEVEL = 3
};

struct PolarityChannel {
    uint8_t pinA;
    uint8_t pinB;
    bool isRunning;
    PolarityState state;
    unsigned long timeA_sec;
    unsigned long timeB_sec;
    unsigned long deadband_sec;
    unsigned long lastChangeMs;
    unsigned long lastSecTickMs;
    unsigned long cycleCount;

    // Tiempos acumulados de funcionamiento (en segundos)
    unsigned long totalSecA;
    unsigned long totalSecB;

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
        lastSecTickMs = millis();
        cycleCount = 0;
        totalSecA = 0;
        totalSecB = 0;
    }

    void setRelays(bool aOn, bool bOn) {
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

        // Acumular tiempo de funcionamiento en vivo por polaridad
        if (currentMs - lastSecTickMs >= 1000UL) {
            unsigned long elapsedSec = (currentMs - lastSecTickMs) / 1000UL;
            lastSecTickMs = currentMs;
            if (state == POL_A) totalSecA += elapsedSec;
            else if (state == POL_B) totalSecB += elapsedSec;
        }

        switch (state) {
            case POL_STOPPED:
                state = POL_A;
                setRelays(true, false);
                lastChangeMs = currentMs;
                lastSecTickMs = currentMs;
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
                    lastSecTickMs = currentMs;
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
                    lastSecTickMs = currentMs;
                }
                break;
        }
    }

    unsigned long getTotalSecSum() {
        return totalSecA + totalSecB;
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

String deviceId = "esp32_01"; 
String firmwareVer = "v5.1";

ArieteChannel ariete1;
PolarityChannel polarity1;
PolarityChannel polarity2;

bool hasLevel = true;
bool useLevelSensor = false;
unsigned long syncIntervalMs = AWS_SYNC_INTERVAL_MS;
unsigned long lastAwsSyncMs = 0;
unsigned long lastNvsSaveMs = 0;

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

    Serial.println("\n=== ESP32 Multi-Channel Controller v5.1 ===");
    Serial.printf("=== Endpoint AWS: %s\n", AWS_API_ENDPOINT);

    initHardware();
    loadSettingsFromNVS();

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

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        attempts++;
    }

    startWiFiPortal();
}

void loop() {
    if (apPortalActive) {
        dnsServer.processNextRequest();
        server.handleClient();
    }

    hasLevel = checkLevelSensor();

    ariete1.update(hasLevel, useLevelSensor);
    polarity1.update();
    polarity2.update();

    // Autoguardado periódico de tiempos acumulados en NVS cada 60 segundos
    if (millis() - lastNvsSaveMs >= 60000UL) {
        lastNvsSaveMs = millis();
        saveSettingsToNVS();
    }

    // Sincronización AWS
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

    ariete1.timeOnSec = preferences.getULong("a1_ton", 5);
    ariete1.timeOffSec = preferences.getULong("a1_toff", 5);
    ariete1.cycleCount = preferences.getULong("a1_cyc", 0);
    ariete1.isRunning = preferences.getBool("a1_run", false);

    polarity1.timeA_sec = preferences.getULong("p1_ta", 25200);
    polarity1.timeB_sec = preferences.getULong("p1_tb", 25200);
    polarity1.deadband_sec = preferences.getULong("p1_td", 3);
    polarity1.cycleCount = preferences.getULong("p1_cyc", 0);
    polarity1.totalSecA = preferences.getULong("p1_tota", 0);
    polarity1.totalSecB = preferences.getULong("p1_totb", 0);
    polarity1.isRunning = preferences.getBool("p1_run", false);

    polarity2.timeA_sec = preferences.getULong("p2_ta", 60);
    polarity2.timeB_sec = preferences.getULong("p2_tb", 60);
    polarity2.deadband_sec = preferences.getULong("p2_td", 3);
    polarity2.cycleCount = preferences.getULong("p2_cyc", 0);
    polarity2.totalSecA = preferences.getULong("p2_tota", 0);
    polarity2.totalSecB = preferences.getULong("p2_totb", 0);
    polarity2.isRunning = preferences.getBool("p2_run", false);

    useLevelSensor = preferences.getBool("use_sensor", false);

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
    preferences.putULong("p1_tota", polarity1.totalSecA);
    preferences.putULong("p1_totb", polarity1.totalSecB);
    preferences.putBool("p1_run", polarity1.isRunning);

    preferences.putULong("p2_ta", polarity2.timeA_sec);
    preferences.putULong("p2_tb", polarity2.timeB_sec);
    preferences.putULong("p2_td", polarity2.deadband_sec);
    preferences.putULong("p2_cyc", polarity2.cycleCount);
    preferences.putULong("p2_tota", polarity2.totalSecA);
    preferences.putULong("p2_totb", polarity2.totalSecB);
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

    JsonObject a1 = doc.createNestedObject("ariete_1");
    a1["is_running"] = ariete1.isRunning;
    a1["state"] = ariete1.getStateString();
    a1["time_on"] = ariete1.timeOnSec;
    a1["time_off"] = ariete1.timeOffSec;
    a1["cycle_count"] = ariete1.cycleCount;
    a1["remaining_sec"] = ariete1.getRemainingSec();

    JsonObject p1 = doc.createNestedObject("polarity_1");
    p1["is_running"] = polarity1.isRunning;
    p1["state"] = polarity1.getStateString();
    p1["time_a"] = polarity1.timeA_sec;
    p1["time_dead"] = polarity1.deadband_sec;
    p1["time_b"] = polarity1.timeB_sec;
    p1["cycle_count"] = polarity1.cycleCount;
    p1["total_sec_a"] = polarity1.totalSecA;
    p1["total_sec_b"] = polarity1.totalSecB;
    p1["total_sec_sum"] = polarity1.getTotalSecSum();
    p1["remaining_sec"] = polarity1.getRemainingSec();

    JsonObject p2 = doc.createNestedObject("polarity_2");
    p2["is_running"] = polarity2.isRunning;
    p2["state"] = polarity2.getStateString();
    p2["time_a"] = polarity2.timeA_sec;
    p2["time_dead"] = polarity2.deadband_sec;
    p2["time_b"] = polarity2.timeB_sec;
    p2["cycle_count"] = polarity2.cycleCount;
    p2["total_sec_a"] = polarity2.totalSecA;
    p2["total_sec_b"] = polarity2.totalSecB;
    p2["total_sec_sum"] = polarity2.getTotalSecSum();
    p2["remaining_sec"] = polarity2.getRemainingSec();

    String jsonOutput;
    serializeJson(doc, jsonOutput);

    int httpCode = http.POST(jsonOutput);
    if (httpCode == 200) {
        parseAWSResponse(http.getString());
    }
    http.end();
}

void parseAWSResponse(String payload) {
    StaticJsonDocument<1024> doc;
    if (deserializeJson(doc, payload)) return;

    bool needsSave = false;

    if (doc.containsKey("cmd_ariete_1")) {
        JsonObject a1Cmd = doc["cmd_ariete_1"];
        if (a1Cmd.containsKey("is_running")) ariete1.isRunning = a1Cmd["is_running"];
        if (a1Cmd.containsKey("time_on")) ariete1.timeOnSec = a1Cmd["time_on"];
        if (a1Cmd.containsKey("time_off")) ariete1.timeOffSec = a1Cmd["time_off"];
        if (a1Cmd.containsKey("reset_cycles") && a1Cmd["reset_cycles"].as<bool>()) ariete1.cycleCount = 0;
        needsSave = true;
    }

    if (doc.containsKey("cmd_polarity_1")) {
        JsonObject p1Cmd = doc["cmd_polarity_1"];
        if (p1Cmd.containsKey("is_running")) polarity1.isRunning = p1Cmd["is_running"];
        if (p1Cmd.containsKey("time_a")) polarity1.timeA_sec = p1Cmd["time_a"];
        if (p1Cmd.containsKey("time_dead")) polarity1.deadband_sec = p1Cmd["time_dead"];
        if (p1Cmd.containsKey("time_b")) polarity1.timeB_sec = p1Cmd["time_b"];
        if (p1Cmd.containsKey("reset_cycles") && p1Cmd["reset_cycles"].as<bool>()) polarity1.cycleCount = 0;
        if (p1Cmd.containsKey("reset_totals") && p1Cmd["reset_totals"].as<bool>()) {
            polarity1.totalSecA = 0;
            polarity1.totalSecB = 0;
        }
        needsSave = true;
    }

    if (doc.containsKey("cmd_polarity_2")) {
        JsonObject p2Cmd = doc["cmd_polarity_2"];
        if (p2Cmd.containsKey("is_running")) polarity2.isRunning = p2Cmd["is_running"];
        if (p2Cmd.containsKey("time_a")) polarity2.timeA_sec = p2Cmd["time_a"];
        if (p2Cmd.containsKey("time_dead")) polarity2.deadband_sec = p2Cmd["time_dead"];
        if (p2Cmd.containsKey("time_b")) polarity2.timeB_sec = p2Cmd["time_b"];
        if (p2Cmd.containsKey("reset_cycles") && p2Cmd["reset_cycles"].as<bool>()) polarity2.cycleCount = 0;
        if (p2Cmd.containsKey("reset_totals") && p2Cmd["reset_totals"].as<bool>()) {
            polarity2.totalSecA = 0;
            polarity2.totalSecB = 0;
        }
        needsSave = true;
    }

    if (doc.containsKey("use_sensor")) {
        useLevelSensor = doc["use_sensor"];
        needsSave = true;
    }

    if (needsSave) {
        saveSettingsToNVS();
    }

    if (doc.containsKey("cmd_ota_update") && doc["cmd_ota_update"].as<bool>()) {
        if (doc.containsKey("ota_url")) {
            String url = doc["ota_url"].as<String>();
            if (url.length() > 0) performHTTPUpdate(url);
        }
    }
}

void startWiFiPortal() {
    apPortalActive = true;
    dnsServer.start(53, "*", WiFi.softAPIP());
    server.on("/", HTTP_GET, []() {
        server.send(200, "text/html", "<h2>ESP32 v5.1 Multi-Channel Portal Active</h2>");
    });
    server.begin();
}

void sendOTAProgressToAWS(int percent, String statusMsg) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    String url = String(AWS_API_ENDPOINT) + "?api_key=" + String(AWS_API_KEY);
    if (!http.begin(client, url)) return;
    http.setTimeout(2000);
    http.addHeader("Content-Type", "text/plain");
    http.addHeader("x-api-key", AWS_API_KEY);

    StaticJsonDocument<256> doc;
    doc["client_type"] = "esp32";
    doc["device_id"] = deviceId;
    doc["firmware_ver"] = firmwareVer;
    doc["state"] = "UPDATING_OTA";
    doc["ota_progress"] = percent;
    doc["ota_status"] = statusMsg;

    String body;
    serializeJson(doc, body);
    http.POST(body);
    http.end();
}

void performHTTPUpdate(String otaUrl) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;

    Serial.printf("[OTA] Descargando desde: %s\n", otaUrl.c_str());
    sendOTAProgressToAWS(5, "Iniciando descarga por Wi-Fi...");

    http.begin(client, otaUrl);
    http.setTimeout(15000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
        int contentLength = http.getSize();
        if (contentLength > 0) {
            if (Update.begin(contentLength)) {
                sendOTAProgressToAWS(30, "Descargando e instalando firmware...");
                WiFiClient* stream = http.getStreamPtr();
                size_t written = Update.writeStream(*stream);

                if (written == (size_t)contentLength) {
                    if (Update.end(true)) {
                        Serial.println("[OTA Exito] ¡Firmware flasheado correctamente!");
                        sendOTAProgressToAWS(100, "¡Instalado con éxito! Reiniciando ESP32...");
                        http.end();
                        delay(1000);
                        ESP.restart();
                        return;
                    } else {
                        Serial.printf("[OTA Error] Error en Update.end(): %s\n", Update.errorString());
                        sendOTAProgressToAWS(0, "Error al finalizar flasheo");
                    }
                } else {
                    Serial.printf("[OTA Error] Bytes incompletos: %d / %d\n", (int)written, contentLength);
                    sendOTAProgressToAWS(0, "Descarga incompleta");
                }
            } else {
                Serial.println("[OTA Error] Memoria insuficiente para Update.begin()");
                sendOTAProgressToAWS(0, "Espacio flash insuficiente");
            }
        } else {
            Serial.println("[OTA Error] Tamaño de contenido inválido (Content-Length)");
            sendOTAProgressToAWS(0, "Tamaño de archivo inválido");
        }
    } else {
        Serial.printf("[OTA Error] Error HTTP GET: %d\n", httpCode);
        sendOTAProgressToAWS(0, "Error HTTP " + String(httpCode));
    }
    http.end();
}
