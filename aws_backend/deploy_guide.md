# Guía de Despliegue en AWS (DynamoDB + Lambda + API Gateway)

A continuación se detallan los pasos para desplegar el backend serverless en AWS usando la consola de AWS o AWS CLI.

---

## Paso 1: Crear la tabla DynamoDB

1. Entra a la consola de **Amazon DynamoDB**.
2. Haz clic en **Crear tabla**.
3. **Nombre de la tabla**: `ESP32_ControlTable`
4. **Clave de partición (Partition Key)**: `device_id` (Tipo: `String` / `S`).
5. Configuración de capacidad: **Pago por petición (On-Demand)**.
6. Haz clic en **Crear tabla**.

---

## Paso 2: Crear la función AWS Lambda

1. Entra a la consola de **AWS Lambda** y haz clic en **Crear una función**.
2. Selecciona **Crear desde cero**:
   - **Nombre de la función**: `ESP32_CycleController_Lambda`
   - **Tiempo de ejecución (Runtime)**: `Python 3.11` (o 3.9/3.10/3.12)
3. En **Permisos**, asegúrate de que el rol de ejecución de la Lambda tenga la política `AmazonDynamoDBFullAccess` (o permisos de Lectura/Escritura sobre la tabla `ESP32_ControlTable`).
4. Haz clic en **Crear función**.
5. Copia y pega el contenido del archivo `aws_backend/lambda_function.py` en el editor de código de la consola Lambda y presiona **Deploy**.

---

## Paso 3: Configurar AWS API Gateway (REST API)

1. Entra a la consola de **API Gateway** y haz clic en **Crear API**.
2. Selecciona **REST API** -> **Construir**.
3. **Nombre del API**: `ESP32_CycleController_API`.
4. Crear un Recurso:
   - Haz clic en **Acciones** -> **Crear recurso**.
   - Nombre del recurso: `esp32`.
   - Ruta del recurso: `/esp32`.
   - Marca la casilla **Habilitar API Gateway CORS**.
5. Crear Métodos:
   - En `/esp32`, añade el método **ANY** (o métodos individuales `GET`, `POST`, `OPTIONS`).
   - Tipo de integración: **Función Lambda**.
   - Selecciona la región y escribe el nombre de tu Lambda (`ESP32_CycleController_Lambda`).
   - Marca la casilla **Usar la integración Proxy de Lambda**.
6. Desplegar la API:
   - Haz clic en **Acciones** -> **Desplegar la API**.
   - **Etapa de despliegue**: `prod`.
7. Copia el **URL de invocación** generado (Ejemplo: `https://xxxxxx.execute-api.us-east-1.amazonaws.com/prod/esp32`).

---

## Paso 4: Actualizar las URLs de Invocación

1. **En el ESP32**: Pega la URL en `esp32_firmware/config.h` en la constante `AWS_API_ENDPOINT`.
2. **En la Web App**: Pega la URL en `web_dashboard/app.js` en la variable `AWS_API_URL`.

---

## Paso 5: Alojar la Web App en AWS (Opcional - S3 + CloudFront / Amplify)

1. Ve a **Amazon S3** y crea un Bucket (ej: `esp32-cycle-controller-web`).
2. Habilita el **Alojamiento de sitios web estáticos**.
3. Sube los archivos `index.html` y `app.js` de la carpeta `web_dashboard/`.
4. Accede a tu sitio web mediante la URL pública del bucket o AWS Amplify.
