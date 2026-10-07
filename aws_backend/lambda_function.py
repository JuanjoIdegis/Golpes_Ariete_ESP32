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
    headers = {
        "Access-Control-Allow-Origin": "*",
        "Access-Control-Allow-Headers": "Content-Type,X-Amz-Date,Authorization,X-Api-Key,X-Amz-Security-Token",
        "Access-Control-Allow-Methods": "GET,POST,OPTIONS"
    }

    try:
        http_method = event.get("httpMethod") or event.get("requestContext", {}).get("http", {}).get("method")
        if not http_method:
            if "client_type" in event or "cmd_ariete_1" in event or "cmd_polarity_1" in event or "cmd_polarity_2" in event or "cmd_delete_device" in event or "target_running" in event:
                http_method = "POST"
            else:
                http_method = "GET"

        if http_method == "OPTIONS":
            return {"statusCode": 200, "headers": headers, "body": ""}

        query_params = event.get("queryStringParameters") or {}
        download_type = query_params.get("download")

        if download_type == "firmware":
            return {
                "statusCode": 404,
                "headers": headers,
                "body": json.dumps({"error": "Direct b64 download disabled. Use raw GitHub URL for OTA."})
            }

        if http_method == "GET":
            target_device_id = query_params.get("device_id", "esp32_01")
            current_time = int(time.time())

            if target_device_id == "all":
                response = table.scan()
                items = response.get("Items", [])
                for item in items:
                    last_seen = int(item.get("last_seen", 0))
                    item["is_online"] = (last_seen > 0 and (current_time - last_seen) <= 20)
                    item["server_time"] = current_time

                items.sort(key=lambda x: str(x.get("device_id", "")))
                return {
                    "statusCode": 200,
                    "headers": headers,
                    "body": json.dumps(items, cls=DecimalEncoder)
                }

            response = table.get_item(Key={"device_id": target_device_id})
            item = response.get("Item")

            if not item:
                item = {
                    "device_id": target_device_id,
                    "device_name": f"Dispositivo {target_device_id.upper()}",
                    "state": "STOPPED",
                    "is_running": False,
                    "has_level": True,
                    "use_sensor": False,
                    "time_on": 5,
                    "time_off": 5,
                    "cycle_count": 0,
                    "remaining_sec": 0,
                    "last_seen": 0,
                    "firmware_ver": "v5.0"
                }

            last_seen = int(item.get("last_seen", 0))
            item["is_online"] = (last_seen > 0 and (current_time - last_seen) <= 20)
            item["server_time"] = current_time

            return {
                "statusCode": 200,
                "headers": headers,
                "body": json.dumps(item, cls=DecimalEncoder)
            }

        elif http_method == "POST":
            if "body" in event:
                body_raw = event.get("body")
                if isinstance(body_raw, str):
                    body = json.loads(body_raw) if body_raw else {}
                elif isinstance(body_raw, dict):
                    body = body_raw
                else:
                    body = {}
            else:
                body = event if isinstance(event, dict) else {}
            
            client_type = body.get("client_type", "esp32")
            device_id = body.get("device_id", "esp32_01")
            device_name = body.get("device_name", f"Dispositivo {device_id.upper()}")

            if client_type == "web":
                if body.get("cmd_delete_device") or body.get("cmd_delete"):
                    target_id = str(body.get("target_device_id", device_id))
                    table.delete_item(Key={"device_id": target_id})
                    return {
                        "statusCode": 200,
                        "headers": headers,
                        "body": json.dumps({"status": "success", "message": f"Dispositivo {target_id} eliminado exitosamente"})
                    }

                update_expr = []
                expr_attr_values = {}
                expr_attr_names = {}

                if "device_name" in body:
                    update_expr.append("#d_name = :d_name")
                    expr_attr_values[":d_name"] = str(body["device_name"])
                    expr_attr_names["#d_name"] = "device_name"

                if "cmd_ariete_1" in body:
                    update_expr.append("#cmd_a1 = :cmd_a1")
                    expr_attr_values[":cmd_a1"] = body["cmd_ariete_1"]
                    expr_attr_names["#cmd_a1"] = "cmd_ariete_1"
                    if isinstance(body["cmd_ariete_1"], dict) and "is_running" in body["cmd_ariete_1"]:
                        val = bool(body["cmd_ariete_1"]["is_running"])
                        update_expr.append("is_running = :a1_run")
                        update_expr.append("#t_run = :a1_run")
                        expr_attr_values[":a1_run"] = val
                        expr_attr_names["#t_run"] = "target_running"

                if "cmd_polarity_1" in body:
                    update_expr.append("#cmd_p1 = :cmd_p1")
                    expr_attr_values[":cmd_p1"] = body["cmd_polarity_1"]
                    expr_attr_names["#cmd_p1"] = "cmd_polarity_1"
                    if isinstance(body["cmd_polarity_1"], dict) and "is_running" in body["cmd_polarity_1"]:
                        val = bool(body["cmd_polarity_1"]["is_running"])
                        update_expr.append("is_running = :p1_run")
                        update_expr.append("#t_run = :p1_run")
                        expr_attr_values[":p1_run"] = val
                        expr_attr_names["#t_run"] = "target_running"

                if "cmd_polarity_2" in body:
                    update_expr.append("#cmd_p2 = :cmd_p2")
                    expr_attr_values[":cmd_p2"] = body["cmd_polarity_2"]
                    expr_attr_names["#cmd_p2"] = "cmd_polarity_2"
                    if isinstance(body["cmd_polarity_2"], dict) and "is_running" in body["cmd_polarity_2"]:
                        val = bool(body["cmd_polarity_2"]["is_running"])
                        update_expr.append("is_running = :p2_run")
                        update_expr.append("#t_run = :p2_run")
                        expr_attr_values[":p2_run"] = val
                        expr_attr_names["#t_run"] = "target_running"

                if "use_sensor" in body:
                    update_expr.append("use_sensor = :u_sens")
                    expr_attr_values[":u_sens"] = bool(body["use_sensor"])

                if "sync_interval_ms" in body:
                    update_expr.append("sync_interval_ms = :s_ms")
                    expr_attr_values[":s_ms"] = Decimal(str(body["sync_interval_ms"]))

                if "time_on" in body:
                    update_expr.append("#t_on = :t_on")
                    expr_attr_values[":t_on"] = Decimal(str(body["time_on"]))
                    expr_attr_names["#t_on"] = "target_time_on"

                if "time_off" in body:
                    update_expr.append("#t_off = :t_off")
                    expr_attr_values[":t_off"] = Decimal(str(body["time_off"]))
                    expr_attr_names["#t_off"] = "target_time_off"

                if "ota_url" in body:
                    update_expr.append("#ota_u = :ota_u, #ota_c = :ota_c")
                    expr_attr_values[":ota_u"] = str(body["ota_url"])
                    expr_attr_values[":ota_c"] = True
                    expr_attr_names["#ota_u"] = "ota_url"
                    expr_attr_names["#ota_c"] = "cmd_ota_update"

                if ("target_running" in body or "is_running" in body) and "cmd_ariete_1" not in body and "cmd_polarity_1" not in body and "cmd_polarity_2" not in body:
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
                current_timestamp = int(time.time())
                firmware_ver = str(body.get("firmware_ver", "v5.0"))
                has_level = bool(body.get("has_level", True))
                use_sensor = bool(body.get("use_sensor", False))
                is_running = bool(body.get("is_running", False))
                state = str(body.get("state", "STOPPED"))
                time_on = Decimal(str(body.get("time_on", 5)))
                time_off = Decimal(str(body.get("time_off", 5)))
                cycle_count = Decimal(str(body.get("cycle_count", 0)))
                remaining_sec = Decimal(str(body.get("remaining_sec", 0)))

                ariete_1 = body.get("ariete_1", {})
                polarity_1 = body.get("polarity_1", {})
                polarity_2 = body.get("polarity_2", {})

                # Fallback retrocompatible para firmwares v4.5 (si ariete_1 no viene en el payload)
                if not ariete_1:
                    ariete_1 = {
                        "is_running": is_running,
                        "state": state,
                        "time_on": time_on,
                        "time_off": time_off,
                        "cycle_count": cycle_count,
                        "remaining_sec": remaining_sec
                    }

                table.update_item(
                    Key={"device_id": device_id},
                    UpdateExpression="SET device_name = if_not_exists(device_name, :dn), firmware_ver = :fw, has_level = :hl, use_sensor = :us, last_seen = :ls, is_running = :ir, #st = :st, time_on = :ton, time_off = :toff, cycle_count = :cyc, remaining_sec = :rem, ariete_1 = :a1, polarity_1 = :p1, polarity_2 = :p2, request_count = if_not_exists(request_count, :zero) + :inc",
                    ExpressionAttributeValues={
                        ":dn": device_name,
                        ":fw": firmware_ver,
                        ":hl": has_level,
                        ":us": use_sensor,
                        ":ls": current_timestamp,
                        ":ir": is_running,
                        ":st": state,
                        ":ton": time_on,
                        ":toff": time_off,
                        ":cyc": cycle_count,
                        ":rem": remaining_sec,
                        ":a1": ariete_1,
                        ":p1": polarity_1,
                        ":p2": polarity_2,
                        ":zero": Decimal("0"),
                        ":inc": Decimal("1")
                    },
                    ExpressionAttributeNames={
                        "#st": "state"
                    }
                )

                response = table.get_item(Key={"device_id": device_id})
                item = response.get("Item", {})

                cmd_a1 = item.get("cmd_ariete_1")
                cmd_p1 = item.get("cmd_polarity_1")
                cmd_p2 = item.get("cmd_polarity_2")
                cmd_reset = item.get("cmd_reset_cycles", False)
                cmd_ota = item.get("cmd_ota_update", False)
                ota_url = item.get("ota_url", "")
                sync_ms = item.get("sync_interval_ms", 5000)
                target_running = item.get("target_running")

                res_payload = {
                    "device_id": device_id,
                    "sync_interval_ms": sync_ms,
                    "cmd_ota_update": cmd_ota,
                    "ota_url": ota_url
                }

                if target_running is not None:
                    res_payload["target_running"] = target_running

                if cmd_a1: res_payload["cmd_ariete_1"] = cmd_a1
                if cmd_p1: res_payload["cmd_polarity_1"] = cmd_p1
                if cmd_p2: res_payload["cmd_polarity_2"] = cmd_p2
                if cmd_reset: res_payload["cmd_reset_cycles"] = cmd_reset

                clear_expr = []
                clear_vals = {}
                if cmd_reset:
                    clear_expr.append("cmd_reset_cycles = :f")
                    clear_vals[":f"] = False
                if cmd_ota:
                    clear_expr.append("cmd_ota_update = :f")
                    clear_vals[":f"] = False
                if cmd_a1:
                    clear_expr.append("cmd_ariete_1 = :null")
                    clear_vals[":null"] = None
                if cmd_p1:
                    clear_expr.append("cmd_polarity_1 = :null")
                    clear_vals[":null"] = None
                if cmd_p2:
                    clear_expr.append("cmd_polarity_2 = :null")
                    clear_vals[":null"] = None

                if clear_expr:
                    table.update_item(
                        Key={"device_id": device_id},
                        UpdateExpression="SET " + ", ".join(clear_expr),
                        ExpressionAttributeValues=clear_vals
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
