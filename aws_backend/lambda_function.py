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
    Función AWS Lambda exclusiva para Golpes Ariete.
    Soporta HTTP API v2 y REST API v1 de API Gateway + Seguridad Wiz x-api-key.
    """
    headers = {
        "Content-Type": "application/json",
        "Access-Control-Allow-Origin": "*",
        "Access-Control-Allow-Headers": "Content-Type,X-Amz-Date,Authorization,X-Api-Key,X-Amz-Security-Token",
        "Access-Control-Allow-Methods": "OPTIONS,GET,POST"
    }
    
    # Detección universal del método HTTP (para HTTP API v2 y REST API v1)
    http_method = event.get("httpMethod") or event.get("requestContext", {}).get("http", {}).get("method", "GET")
    
    if http_method == "OPTIONS":
        return {
            "statusCode": 200,
            "headers": headers,
            "body": json.dumps({"message": "CORS OK"})
        }

    # Validar API Key exclusiva del proyecto Golpes Ariete
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
            response = table.get_item(Key={"device_id": "esp32_01"})
            item = response.get("Item", {
                "device_id": "esp32_01",
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
            return {
                "statusCode": 200,
                "headers": headers,
                "body": json.dumps(item, cls=DecimalEncoder)
            }

        elif http_method == "POST":
            body_str = event.get("body", "{}")
            body = json.loads(body_str) if body_str else {}
            
            client_type = body.get("client_type", "esp32")

            if client_type == "web":
                update_expr = []
                expr_attr_values = {}
                expr_attr_names = {}

                if "time_on" in body:
                    update_expr.append("#t_on = :t_on")
                    expr_attr_values[":t_on"] = Decimal(str(body["time_on"]))
                    expr_attr_names["#t_on"] = "target_time_on"

                if "time_off" in body:
                    update_expr.append("#t_off = :t_off")
                    expr_attr_values[":t_off"] = Decimal(str(body["time_off"]))
                    expr_attr_names["#t_off"] = "target_time_off"

                if "is_running" in body:
                    update_expr.append("#t_run = :t_run")
                    expr_attr_values[":t_run"] = bool(body["is_running"])
                    expr_attr_names["#t_run"] = "target_running"

                if "use_sensor" in body:
                    update_expr.append("#u_sens = :u_sens")
                    expr_attr_values[":u_sens"] = bool(body["use_sensor"])
                    expr_attr_names["#u_sens"] = "use_sensor"

                if "cmd_reset_cycles" in body:
                    update_expr.append("#reset = :reset")
                    expr_attr_values[":reset"] = bool(body["cmd_reset_cycles"])
                    expr_attr_names["#reset"] = "cmd_reset_cycles"

                if update_expr:
                    table.update_item(
                        Key={"device_id": "esp32_01"},
                        UpdateExpression="SET " + ", ".join(update_expr),
                        ExpressionAttributeValues=expr_attr_values,
                        ExpressionAttributeNames=expr_attr_names
                    )

                # Incrementar contador de peticiones
                table.update_item(
                    Key={"device_id": "esp32_01"},
                    UpdateExpression="ADD request_count :inc",
                    ExpressionAttributeValues={":inc": Decimal("1")}
                )

                return {
                    "statusCode": 200,
                    "headers": headers,
                    "body": json.dumps({"status": "success", "message": "Configuración actualizada"})
                }

            else:
                current_timestamp = int(time.time())
                state = body.get("state", "STOPPED")
                is_running = body.get("is_running", False)
                has_level = body.get("has_level", True)
                use_sensor = body.get("use_sensor", False)
                time_on = body.get("time_on", 5)
                time_off = body.get("time_off", 5)
                cycle_count = body.get("cycle_count", 0)
                remaining_sec = body.get("remaining_sec", 0)

                table.update_item(
                    Key={"device_id": "esp32_01"},
                    UpdateExpression="SET #s = :s, is_running = :r, has_level = :hl, use_sensor = :us, time_on = :ton, time_off = :toff, cycle_count = :cc, remaining_sec = :rem, last_seen = :ls ADD request_count :inc",
                    ExpressionAttributeNames={"#s": "state"},
                    ExpressionAttributeValues={
                        ":s": state,
                        ":r": is_running,
                        ":hl": bool(has_level),
                        ":us": bool(use_sensor),
                        ":ton": Decimal(str(time_on)),
                        ":toff": Decimal(str(time_off)),
                        ":cc": Decimal(str(cycle_count)),
                        ":rem": Decimal(str(remaining_sec)),
                        ":ls": current_timestamp,
                        ":inc": Decimal("1")
                    }
                )

                response = table.get_item(Key={"device_id": "esp32_01"})
                item = response.get("Item", {})

                cmd_reset = item.get("cmd_reset_cycles", False)
                
                res_payload = {
                    "target_time_on": item.get("target_time_on", time_on),
                    "target_time_off": item.get("target_time_off", time_off),
                    "target_running": item.get("target_running", is_running),
                    "use_sensor": item.get("use_sensor", use_sensor),
                    "cmd_reset_cycles": cmd_reset,
                    "request_count": item.get("request_count", 0)
                }

                if cmd_reset:
                    table.update_item(
                        Key={"device_id": "esp32_01"},
                        UpdateExpression="SET cmd_reset_cycles = :f",
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
