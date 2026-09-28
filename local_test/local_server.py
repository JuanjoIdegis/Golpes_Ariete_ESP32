#!/usr/bin/env python3
"""
Servidor HTTP Local de Prueba (Emulador de AWS API Gateway + DynamoDB)
No requiere instalar nada adicional (usa el módulo http.server de Python).
"""

import json
import time
from http.server import HTTPServer, BaseHTTPRequestHandler
import socket

# Estado global simulado de la base de datos
db_state = {
    "device_id": "esp32_01",
    "state": "STOPPED",
    "is_running": False,
    "has_level": True,
    "time_on": 5,
    "time_off": 5,
    "cycle_count": 0,
    "remaining_sec": 0,
    "target_time_on": 5,
    "target_time_off": 5,
    "target_running": False,
    "cmd_reset_cycles": False,
    "last_seen": 0
}

class LocalTestRequestHandler(BaseHTTPRequestHandler):

    def _set_headers(self, status_code=200):
        self.send_response(status_code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.end_headers()

    def do_OPTIONS(self):
        self._set_headers(200)

    def do_GET(self):
        self._set_headers(200)
        self.wfile.write(json.dumps(db_state).encode("utf-8"))

    def do_POST(self):
        content_length = int(self.headers.get("Content-Length", 0))
        post_data = self.rfile.read(content_length) if content_length > 0 else b"{}"

        try:
            body = json.loads(post_data.decode("utf-8"))
        except Exception:
            body = {}

        client_type = body.get("client_type", "esp32")

        if client_type == "web":
            # Petición enviada desde el Dashboard Web
            if "time_on" in body:
                db_state["target_time_on"] = int(body["time_on"])
            if "time_off" in body:
                db_state["target_time_off"] = int(body["time_off"])
            if "is_running" in body:
                db_state["target_running"] = bool(body["is_running"])
            if "cmd_reset_cycles" in body:
                db_state["cmd_reset_cycles"] = bool(body["cmd_reset_cycles"])

            print(f"[WEB CMD] Configuración actualizada: T_ON={db_state['target_time_on']}s, T_OFF={db_state['target_time_off']}s, Running={db_state['target_running']}")
            
            self._set_headers(200)
            self.wfile.write(json.dumps({"status": "success", "message": "Configuración local actualizada"}).encode("utf-8"))

        else:
            # Petición enviada desde el ESP32
            db_state["state"] = body.get("state", db_state["state"])
            db_state["is_running"] = body.get("is_running", db_state["is_running"])
            db_state["has_level"] = body.get("has_level", db_state["has_level"])
            db_state["time_on"] = body.get("time_on", db_state["time_on"])
            db_state["time_off"] = body.get("time_off", db_state["time_off"])
            db_state["cycle_count"] = body.get("cycle_count", db_state["cycle_count"])
            db_state["remaining_sec"] = body.get("remaining_sec", db_state["remaining_sec"])
            db_state["last_seen"] = int(time.time())

            print(f"[ESP32 TELEMETRY] Estado: {db_state['state']} | Nivel: {'OK' if db_state['has_level'] else 'SIN NIVEL'} | Ciclos: {db_state['cycle_count']}")

            # Respuesta para el ESP32 con las metas deseadas
            res_payload = {
                "target_time_on": db_state["target_time_on"],
                "target_time_off": db_state["target_time_off"],
                "target_running": db_state["target_running"],
                "cmd_reset_cycles": db_state["cmd_reset_cycles"]
            }

            if db_state["cmd_reset_cycles"]:
                db_state["cmd_reset_cycles"] = False

            self._set_headers(200)
            self.wfile.write(json.dumps(res_payload).encode("utf-8"))

def get_local_ip():
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return "127.0.0.1"

def run_server(port=5000):
    local_ip = get_local_ip()
    server_address = ("0.0.0.0", port)
    httpd = HTTPServer(server_address, LocalTestRequestHandler)
    print("\n=======================================================")
    print("   SERVIDOR LOCAL DE PRUEBA ESP32 - INICIADO")
    print("=======================================================")
    print(f" -> IP de tu PC en la red local: {local_ip}")
    print(f" -> URL para configurar en app.js:   http://localhost:{port}/esp32")
    print(f" -> URL para configurar en config.h:  http://{local_ip}:{port}/esp32")
    print("=======================================================\n")
    print("Esperando conexiones... Presiona Ctrl+C para detener.\n")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nServidor local detenido.")

if __name__ == "__main__":
    run_server()
