/**
 * webserver.h — Веб-сервер и API endpoints
 */

#ifndef WEBSERVER_H
#define WEBSERVER_H

#include <Arduino.h>
#include <WebServer.h>
#include <LittleFS.h>

// Глобальный объект веб-сервера
extern WebServer webServer;
extern bool webServerEnabled;

// Инициализация веб-сервера
void initWebServer();

// Обработчики API
void handleRoot();
void handleStatusAPI();
void handleHistoryAPI();
void handleControlAPI();
bool handleStaticFileWeb();

// Генерация JSON
String getSystemStatusJSON();
String getHistoryJSON();

#endif // WEBSERVER_H
