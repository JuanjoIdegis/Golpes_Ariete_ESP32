import json
import os
import time
import boto3
from decimal import Decimal

class DecimalEncoder(json.JSONEncoder):
    def default(self, o):
        if isinstance(o, Decimal):
            if o % 1 == 0:
                return int(o)
            return float(o)
        return super(DecimalEncoder, self).default(o)

TABLE_NAME = os.environ.get("DYNAMODB_TABLE", "GolpesAriete_ControlTable")
dynamodb = boto3.resource("dynamodb")
table = dynamodb.Table(TABLE_NAME)

def lambda_handler(event, context):
    """
    Función AWS Lambda para Golpes Ariete con soporte multidispositivo (multi-ESP32 por Planta).
    Incluye autenticación Wiz x-api-key y gestión dinámica por device_id.
    """
    headers = {
        "Content-Type": "application/json",
        "Access-Control-Allow-Origin": "*",
        "Access-Control-Allow-Headers": "Content-Type,X-Amz-Date,Authorization,X-Api-Key,X-Amz-Security-Token",
        "Access-Control-Allow-Methods": "OPTIONS,GET,POST"
    }
    
    http_method = event.get("httpMethod") or event.get("requestContext", {}).get("http", {}).get("method", "GET")
    
    if http_method == "OPTIONS":
        return {
            "statusCode": 200,
            "headers": headers,
            "body": json.dumps({"message": "CORS OK"})
        }

    # 1. Validar API Key de Seguridad (Wiz Audit Compliance)
    params = event.get("queryStringParameters") or {}
    req_headers = event.get("headers") or {}
    api_key_val = req_headers.get("x-api-key") or req_headers.get("X-Api-Key") or params.get("api_key")
    EXPECTED_API_KEY = os.environ.get("API_KEY", "GolpesAriete2026SecureKey!")

    if api_key_val != EXPECTED_API_KEY:
        return {
            "statusCode": 401,
            "headers": headers,
            "body": json.dumps({"error": "No autorizado: API Key invalida o ausente"})
        }

    try:
        if http_method == "GET":
            target_device = params.get("device_id")

            current_time = int(time.time())

            if target_device == "all" or target_device == "*":
                # Escanear y listar todos los dispositivos (para el selector de planta de la Web App)
                scan_res = table.scan()
                items = scan_res.get("Items", [])
                for it in items:
                    last_seen = int(it.get("last_seen", 0))
                    it["is_online"] = (current_time - last_seen) <= 20
                    it["server_time"] = current_time
                return {
                    "statusCode": 200,
                    "headers": headers,
                    "body": json.dumps(items, cls=DecimalEncoder)
                }
            
            # Consultar un dispositivo específico (por defecto esp32_01 o Planta 1)
            device_id = target_device or "esp32_01"
            response = table.get_item(Key={"device_id": device_id})
            item = response.get("Item", {
                "device_id": device_id,
                "device_name": f"Dispositivo {device_id.upper()}",
                "state": "STOPPED",
                "is_running": False,
                "has_level": True,
                "use_sensor": False,
                "time_on": 5,
                "time_off": 5,
                "cycle_count": 0,
                "remaining_sec": 0,
                "request_count": 0,
                "target_time_on": 5,
                "target_time_off": 5,
                "target_running": False,
                "cmd_reset_cycles": False,
                "last_seen": 0
            })

            last_seen = int(item.get("last_seen", 0))
            item["is_online"] = (current_time - last_seen) <= 20
            item["server_time"] = current_time

            return {
                "statusCode": 200,
                "headers": headers,
                "body": json.dumps(item, cls=DecimalEncoder)
            }

        elif http_method == "POST":
            body_str = event.get("body", "{}")
            body = json.loads(body_str) if body_str else {}
            
            client_type = body.get("client_type", "esp32")
            device_id = body.get("device_id", "esp32_01")
            device_name = body.get("device_name", f"Dispositivo {device_id.upper()}")

            if client_type == "web":
                update_expr = []
                expr_attr_values = {}
                expr_attr_names = {}

                if "device_name" in body:
                    update_expr.append("#d_name = :d_name")
                    expr_attr_values[":d_name"] = str(body["device_name"])
                    expr_attr_names["#d_name"] = "device_name"

                if "time_on" in body:
                    update_expr.append("#t_on = :t_on")
                    expr_attr_values[":t_on"] = Decimal(str(body["time_on"]))
                    expr_attr_names["#t_on"] = "target_time_on"

                if "time_off" in body:
                    update_expr.append("#t_off = :t_off")
                    expr_attr_values[":t_off"] = Decimal(str(body["time_off"]))
                    expr_attr_names["#t_off"] = "target_time_off"

                if "sync_interval_ms" in body:
                    update_expr.append("#s_int = :s_int")
                    expr_attr_values[":s_int"] = Decimal(str(body["sync_interval_ms"]))
                    expr_attr_names["#s_int"] = "sync_interval_ms"

                if "ota_url" in body:
                    update_expr.append("#ota_u = :ota_u, #ota_c = :ota_c")
                    expr_attr_values[":ota_u"] = str(body["ota_url"])
                    expr_attr_values[":ota_c"] = True
                    expr_attr_names["#ota_u"] = "ota_url"
                    expr_attr_names["#ota_c"] = "cmd_ota_update"

                if "target_running" in body or "is_running" in body:
                    target_val = bool(body.get("target_running", body.get("is_running")))
                    update_expr.append("#t_run = :t_run, is_running = :t_run")
                    expr_attr_values[":t_run"] = target_val
                    expr_attr_names["#t_run"] = "target_running"

                if "cmd_reset_cycles" in body:
                    update_expr.append("#reset = :reset")
                    expr_attr_values[":reset"] = bool(body["cmd_reset_cycles"])
                    expr_attr_names["#reset"] = "cmd_reset_cycles"

                if update_expr:
                    table.update_item(
                        Key={"device_id": device_id},
                        UpdateExpression="SET " + ", ".join(update_expr),
                        ExpressionAttributeValues=expr_attr_values,
                        ExpressionAttributeNames=expr_attr_names
                    )

                # Incrementar contador de peticiones de la API
                table.update_item(
                    Key={"device_id": device_id},
                    UpdateExpression="SET request_count = if_not_exists(request_count, :zero) + :inc",
                    ExpressionAttributeValues={":zero": Decimal("0"), ":inc": Decimal("1")}
                )

                return {
                    "statusCode": 200,
                    "headers": headers,
                    "body": json.dumps({"status": "success", "message": f"Configuración de {device_id} actualizada"})
                }

            else:
                # Petición enviada desde el ESP32
                current_timestamp = int(time.time())
                state = body.get("state", "STOPPED")
                is_running = body.get("is_running", False)
                has_level = body.get("has_level", True)
                use_sensor = body.get("use_sensor", False)
                time_on = body.get("time_on", 5)
                time_off = body.get("time_off", 5)
                cycle_count = body.get("cycle_count", 0)
                remaining_sec = body.get("remaining_sec", 0)

                # Actualización de DynamoDB: El ESP32 sólo actualiza su telemetría local (state, cycle_count, etc) sin machacar la orden target_running enviada por la Web
                table.update_item(
                    Key={"device_id": device_id},
                    UpdateExpression="SET #s = :s, device_name = if_not_exists(device_name, :dn), is_running = :r, target_running = if_not_exists(target_running, :r), has_level = :hl, use_sensor = :us, time_on = :ton, target_time_on = if_not_exists(target_time_on, :ton), time_off = :toff, target_time_off = if_not_exists(target_time_off, :toff), cycle_count = :cc, remaining_sec = :rem, last_seen = :ls, request_count = if_not_exists(request_count, :zero) + :inc",
                    ExpressionAttributeNames={"#s": "state"},
                    ExpressionAttributeValues={
                        ":s": state,
                        ":dn": device_name,
                        ":r": is_running,
                        ":hl": bool(has_level),
                        ":us": bool(use_sensor),
                        ":ton": Decimal(str(time_on)),
                        ":toff": Decimal(str(time_off)),
                        ":cc": Decimal(str(cycle_count)),
                        ":rem": Decimal(str(remaining_sec)),
                        ":ls": current_timestamp,
                        ":zero": Decimal("0"),
                        ":inc": Decimal("1")
                    }
                )

                response = table.get_item(Key={"device_id": device_id})
                item = response.get("Item", {})

                cmd_reset = item.get("cmd_reset_cycles", False)
                cmd_ota = item.get("cmd_ota_update", False)
                ota_url = item.get("ota_url", "")
                sync_ms = item.get("sync_interval_ms", 2000)

                res_payload = {
                    "device_id": device_id,
                    "target_time_on": item.get("target_time_on", time_on),
                    "target_time_off": item.get("target_time_off", time_off),
                    "target_running": item.get("target_running", is_running),
                    "use_sensor": item.get("use_sensor", use_sensor),
                    "sync_interval_ms": sync_ms,
                    "cmd_reset_cycles": cmd_reset,
                    "cmd_ota_update": cmd_ota,
                    "ota_url": ota_url,
                    "request_count": item.get("request_count", 0)
                }

                if cmd_reset or cmd_ota:
                    table.update_item(
                        Key={"device_id": device_id},
                        UpdateExpression="SET cmd_reset_cycles = :f, cmd_ota_update = :f",
                        ExpressionAttributeValues={":f": False}
                    )

                return {
                    "statusCode": 200,
                    "headers": headers,
                    "body": json.dumps(res_payload, cls=DecimalEncoder)
                }

    except Exception as e:
        print(f"Error procesando solicitud: {str(e)}")
        return {
            "statusCode": 500,
            "headers": headers,
            "body": json.dumps({"error": str(e)})
        }
