# ☁️ Guía Completa de Despliegue en AWS & Normas de Seguridad (Golpes Ariete)

Este documento contiene la arquitectura exclusiva para el proyecto **Golpes Ariete / Control de Ciclos ESP32**, aislado de cualquier otro proyecto o tabla de laboratorio.

---

## 📌 1. Nombres de Recursos Exclusivos para este Proyecto

Para evitar colisiones y mantener total independencia entre proyectos:

* **Nombre del Proyecto / Repositorio Git**: `Golpes_Ariete_ESP32`
* **Tabla de DynamoDB Exclusiva**: `GolpesAriete_ControlTable`
  * **Partition Key (PK)**: `device_id` (String)
* **Función AWS Lambda Exclusiva**: `GolpesAriete_SyncBackend` (Python 3.12)
* **Endpoint API Gateway Exclusivo**: `/GolpesArieteSync`
* **API Key de Seguridad**: `GolpesAriete2026SecureKey!`
* **Despliegue Frontend AWS Amplify**: *Pendiente de crear app independiente en la consola de AWS Amplify (ej. `https://main.xxxx.amplifyapp.com`)*. No compartir con la URL de LabEngine.

---

## 📊 2. Registro de Peticiones (Request Counter)

El backend de Lambda incrementa automáticamente el contador `request_count` en cada sincronización del ESP32 o interacción desde la Web App.

---

## 🛡️ 3. Normas de Auditoría Wiz Compliance (Aplicadas)

1. **Tabla y Privilegios Acotados**: La función Lambda solo tiene permisos para acceder a la tabla `GolpesAriete_ControlTable`.
2. **Autenticación Obligatoria**: Exige la cabecera `x-api-key: GolpesAriete2026SecureKey!` o el parámetro `?api_key=...`. Peticiones no autorizadas reciben un HTTP 401.
3. **Métrico de Peticiones**: Permite visualizar en el Dashboard el número total de llamadas a la API de AWS realizas.
