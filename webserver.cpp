/**
 * webserver.cpp — Веб-сервер и API endpoints
 */

#include "webserver.h"
#include "control.h"
#include "sensors.h"
#include "telegram.h"
#include <ArduinoJson.h>
#include <WiFi.h>

WebServer webServer(80);
bool webServerEnabled = false;

// История температур
extern float tempHistory[HISTORY_SIZE];
extern float flowHistory[HISTORY_SIZE];
extern float airHistory[HISTORY_SIZE];
extern int powerHistory[HISTORY_SIZE];
extern unsigned long historyTimestamps[HISTORY_SIZE];
extern int historyIndex;

// ==================== ИНИЦИАЛИЗАЦИЯ ====================

void initWebServer() {
    webServer.onNotFound([]() {
        if (!handleStaticFileWeb()) {
            webServer.send(404, "text/plain", "File Not Found");
        }
    });

    webServer.on("/", HTTP_GET, handleRoot);
    webServer.on("/api/status", HTTP_GET, handleStatusAPI);
    webServer.on("/api/history", HTTP_GET, handleHistoryAPI);
    webServer.on("/api/control", HTTP_POST, handleControlAPI);

    webServer.begin();
    Serial.println("✅ Веб-сервер начал прослушивание на порту 80");
    webServerEnabled = true;
}

// ==================== ОБРАБОТЧИКИ ====================

void handleRoot() {
    if (LittleFS.exists("/index.html")) {
        File file = LittleFS.open("/index.html", "r");
        webServer.streamFile(file, "text/html");
        file.close();
    } else {
        String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
        html += "<title>Умный Котёл v4.1</title><style>body{font-family:Arial;margin:20px;}</style></head><body>";
        html += "<h1>Умный Котёл v4.1</h1><p>Файлы не загружены в LittleFS</p><p>Используйте API:</p>";
        html += "<ul><li><a href='/api/status'>/api/status</a></li><li><a href='/api/history'>/api/history</a></li></ul>";
        html += "</body></html>";
        webServer.send(200, "text/html", html);
    }
}

void handleStatusAPI() {
    webServer.sendHeader("Access-Control-Allow-Origin", "*");
    webServer.send(200, "application/json", getSystemStatusJSON());
}

void handleHistoryAPI() {
    webServer.sendHeader("Access-Control-Allow-Origin", "*");
    webServer.send(200, "application/json", getHistoryJSON());
}

void handleControlAPI() {
    if (webServer.hasArg("cmd")) {
        String cmd = webServer.arg("cmd");
        String response = "";

        if (cmd == "on") {
            currentMode = MODE_MAINTAIN;
            powerTarget = 0;
            response = "{\"status\":\"ok\",\"message\":\"Авто режим включён\"}";
        } else if (cmd == "off") {
            currentMode = MODE_OFF;
            setPower(0);
            setRelay(false);
            response = "{\"status\":\"ok\",\"message\":\"Котёл выключен\"}";
        } else if (cmd == "manual") {
            currentMode = MODE_MANUAL;
            response = "{\"status\":\"ok\",\"message\":\"Ручной режим включён\"}";
        } else if (cmd.startsWith("temp")) {
            int temp = cmd.substring(4).toInt();
            if (temp >= MIN_TARGET_TEMP && temp <= MAX_TARGET_TEMP) {
                targetReturnTemp = temp;
                response = "{\"status\":\"ok\",\"message\":\"Цель: " + String(temp) + "°C\"}";
            } else {
                response = "{\"status\":\"error\",\"message\":\"Температура должна быть от " +
                           String(MIN_TARGET_TEMP) + " до " + String(MAX_TARGET_TEMP) + "°C\"}";
            }
        } else if (cmd.startsWith("power")) {
            int power = cmd.substring(5).toInt();
            if (power >= 0 && power <= 100) {
                if (currentMode != MODE_MANUAL) {
                    response = "{\"status\":\"error\",\"message\":\"Сначала включите ручной режим\"}";
                } else {
                    setPower(power);
                    response = "{\"status\":\"ok\",\"message\":\"Мощность: " + String(power) + "%\"}";
                }
            }
        } else if (cmd.startsWith("max")) {
            int maxTemp = cmd.substring(3).toInt();
            if (maxTemp >= 60 && maxTemp <= 90) {
                maxFlowTemp = maxTemp;
                response = "{\"status\":\"ok\",\"message\":\"Макс. температура: " + String(maxTemp) + "°C\"}";
            }
        } else if (cmd == "reset") {
            resetSafetyBlock();
            response = "{\"status\":\"ok\",\"message\":\"Ошибки сброшены\"}";
        } else {
            response = "{\"status\":\"error\",\"message\":\"Неизвестная команда\"}";
        }

        saveSettings();
        webServer.sendHeader("Access-Control-Allow-Origin", "*");
        webServer.send(200, "application/json", response);
    } else {
        webServer.send(400, "application/json", "{\"error\":\"No command\"}");
    }
}

bool handleStaticFileWeb() {
    String path = webServer.uri();
    if (path.endsWith("/")) path += "index.html";

    String contentType = "text/plain";
    if (path.endsWith(".html")) contentType = "text/html";
    else if (path.endsWith(".css")) contentType = "text/css";
    else if (path.endsWith(".js")) contentType = "application/javascript";
    else if (path.endsWith(".png")) contentType = "image/png";
    else if (path.endsWith(".jpg")) contentType = "image/jpeg";

    if (LittleFS.exists(path)) {
        File file = LittleFS.open(path, "r");
        webServer.streamFile(file, contentType);
        file.close();
        return true;
    }
    return false;
}

// ==================== JSON ГЕНЕРАТОРЫ ====================

String getSystemStatusJSON() {
    StaticJsonDocument<1024> doc;

    doc["mode"] = getModeString();
    doc["mode_code"] = currentMode;
    doc["target_temp"] = targetReturnTemp;
    doc["max_temp"] = maxFlowTemp;
    doc["power"] = powerTarget;
    doc["relay"] = reportedRelayState;
    doc["actual_relay"] = actualRelayState;
    doc["instant_power"] = bresenhamPower;
    doc["safety"] = safetyBlock;
    doc["safety_reason"] = safetyReason;
    doc["last_error"] = lastError;
    doc["return_temp"] = currentReturnTemp;
    doc["flow_temp"] = currentFlowTemp;
    doc["air_temp"] = currentAirTemp;
    doc["water"] = currentWaterOk;
    doc["uptime"] = getUptimeString();
    doc["ip"] = WiFi.localIP().toString();

    // Расчёт потребления для 4кВт котла
    float consumption_kw = 0;
    if (reportedRelayState && bresenhamPower > 0) {
        consumption_kw = 4.0f * bresenhamPower / 100.0f;
    }
    doc["consumption_kw"] = consumption_kw;
    doc["consumption_w"] = consumption_kw * 1000;

    String out;
    serializeJson(doc, out);
    return out;
}

String getHistoryJSON() {
    StaticJsonDocument<4096> doc;

    JsonArray ts = doc.createNestedArray("timestamps");
    JsonArray rt = doc.createNestedArray("return_temp");
    JsonArray ft = doc.createNestedArray("flow_temp");
    JsonArray at = doc.createNestedArray("air_temp");
    JsonArray pw = doc.createNestedArray("power");

    for (int i = 0; i < HISTORY_SIZE; i++) {
        int idx = (historyIndex + i) % HISTORY_SIZE;
        if (historyTimestamps[idx] > 0) {
            ts.add(historyTimestamps[idx] / 1000);
            rt.add(tempHistory[idx]);
            ft.add(flowHistory[idx]);
            at.add(airHistory[idx]);
            pw.add(powerHistory[idx]);
        }
    }

    String out;
    serializeJson(doc, out);
    return out;
}
