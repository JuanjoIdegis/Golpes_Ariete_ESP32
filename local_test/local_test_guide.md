# Guía de Pruebas en Local (Sin AWS)

Esta guía te permite probar la comunicación completa (**ESP32 + Web Dashboard**) en tu red local antes de subir nada a AWS.

---

## Paso 1: Iniciar el Servidor Local en tu PC

En una terminal en tu ordenador, ejecuta el script de servidor local:

```bash
python local_test/local_server.py
```

Al iniciarse, el script imprimirá en pantalla algo como esto:

```text
=======================================================
   SERVIDOR LOCAL DE PRUEBA ESP32 - INICIADO
=======================================================
 -> IP de tu PC en la red local: 192.168.1.50
 -> URL para configurar en app.js:   http://localhost:5000/esp32
 -> URL para configurar en config.h:  http://192.168.1.50:5000/esp32
=======================================================
```

---

## Paso 2: Configurar la Web App para Local

Abre el archivo `web_dashboard/app.js` y cambia la constante `AWS_API_URL`:

```javascript
const AWS_API_URL = "http://localhost:5000/esp32";
```

Luego, simplemente abre el archivo `web_dashboard/index.html` haciendo doble clic en él o abriéndolo directamente en tu navegador (Chrome, Edge, Firefox).

---

## Paso 3: Configurar el ESP32 para Local

Abre `esp32_firmware/config.h` y configura las credenciales de tu Wi-Fi y la IP de tu PC:

```cpp
#define WIFI_SSID "TU_RED_WIFI_LOCAL"
#define WIFI_PASSWORD "TU_PASSWORD_WIFI"

// Sustituye la IP por la que te mostró el servidor local al arrancar (ej: 192.168.1.50)
#define AWS_API_ENDPOINT "http://192.168.1.50:5000/esp32"
```

> **¡Importante!**: Tu PC y el ESP32 deben estar conectados a la **misma red Wi-Fi**.

---

## Paso 4: Pruebas a Realizar

1. **Prueba del Dashboard Web**:
   - Abre `index.html` en el navegador. Verás que la esquina superior muestra **"Conectado AWS"** (en este caso conectando al servidor local).
   - Haz clic en **INICIAR** o cambia los tiempos ON/OFF. En la consola de tu PC verás los logs de recepción `[WEB CMD]`.

2. **Prueba del ESP32**:
   - Carga el firmware en tu ESP32 mediante el IDE de Arduino.
   - Abre el Monitor Serie (115200 baudios). Verás la conexión Wi-Fi y la máquina de estados alternando relé en ON y OFF.
   - En la consola de tu PC verás los mensajes en tiempo real `[ESP32 TELEMETRY]`.

3. **Prueba del Sensor de Nivel (XKC-Y25-PNP)**:
   - Acerca el sensor a un recipiente con agua o acciona manualmente la entrada `GPIO 5`.
   - Si desconectas o quitas el nivel de agua del sensor, el relé se apagará de inmediato y la interfaz web mostrará **"ALERTA: Sin Nivel"**.

---

## Paso 5: Dar el salto a AWS

Una vez verificado que todo funciona a la perfección en tu mesa de trabajo:
1. Sigue la guía [`aws_backend/deploy_guide.md`](../aws_backend/deploy_guide.md).
2. Cambia la URL local por la URL HTTPS pública de AWS API Gateway en `config.h` y `app.js`.
