#ifndef CONFIG_H
#define CONFIG_H

// Configuración de Wi-Fi
#define WIFI_SSID "iOT"
#define WIFI_PASSWORD "C7Ep2sVF9Bm1"

// Endpoint de AWS API Gateway Cloud Exclusivo (eu-north-1)
#define AWS_API_ENDPOINT "https://d3h13f6kjb.execute-api.eu-north-1.amazonaws.com/default/GolpesAriete_SyncBackend"
#define AWS_API_KEY "GolpesAriete2026SecureKey!"

// Configuración de Hardware - Mapeo de Pines

// 1. Salida Golpe de Ariete (Pulso Cíclico ON/OFF)
#define ARIETE_1_RELAY_PIN 2            // Pin GPIO para el relé de Golpe de Ariete (GPIO 2 LED integrado)
#define RELAY_ACTIVE_HIGH true          // true si los relés se activan en HIGH

// 2. Salidas Inversión de Polaridad 1 (Canal 1 - ej: 7 Horas)
#define POLARITY_1_RELAY_A_PIN 4        // Salida Directa (Polaridad A)
#define POLARITY_1_RELAY_B_PIN 16       // Salida Inversa (Polaridad B)

// 3. Salidas Inversión de Polaridad 2 (Canal 2 - ej: 1 Minuto)
#define POLARITY_2_RELAY_A_PIN 17       // Salida Directa (Polaridad A)
#define POLARITY_2_RELAY_B_PIN 18       // Salida Inversa (Polaridad B)

// Sensor de Nivel sin contacto XKC-Y25-PNP
#define LEVEL_SENSOR_PIN 19             // Pin GPIO para la señal del sensor XKC-Y25-PNP
#define LEVEL_SENSOR_ACTIVE_HIGH true   // PNP: HIGH indica agua presente

// Intervalo de sincronización con AWS en milisegundos (5000 ms = 5 segundos)
#define AWS_SYNC_INTERVAL_MS 5000

#endif // CONFIG_H
