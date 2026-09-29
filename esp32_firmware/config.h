#ifndef CONFIG_H
#define CONFIG_H

// Configuración de Wi-Fi
#define WIFI_SSID "iOT"
#define WIFI_PASSWORD "C7Ep2sVF9Bm1"

// Endpoint de AWS API Gateway Cloud Exclusivo (eu-north-1)
#define AWS_API_ENDPOINT "https://d3h13f6kjb.execute-api.eu-north-1.amazonaws.com/default/GolpesAriete_SyncBackend"
#define AWS_API_KEY "GolpesAriete2026SecureKey!"

// Configuración de Hardware
#define RELAY_PIN 2            // Pin GPIO para controlar el relé o la carga (GPIO 2 LED integrado)
#define RELAY_ACTIVE_HIGH true // true si el relé se activa en HIGH, false si se activa en LOW

// Sensor de Nivel sin contacto XKC-Y25-PNP
#define LEVEL_SENSOR_PIN 5            // Pin GPIO para la señal de salida del sensor XKC-Y25-PNP
#define LEVEL_SENSOR_ACTIVE_HIGH true // PNP: HIGH indica nivel detectado (agua presente), LOW indica sin nivel (tanque vacío)

// Intervalo de sincronización con AWS en milisegundos (5000 ms = 5 segundos para optimizar cuota de peticiones)
#define AWS_SYNC_INTERVAL_MS 5000

#endif // CONFIG_H
