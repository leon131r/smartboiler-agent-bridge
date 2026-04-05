/**
 * WiFi Clock с OLED дисплеем
 * ESP32-C3 (lolin_c3_mini)
 *
 * Функции:
 * - Синхронизация времени через NTP
 * - WiFi + AP режим при потере связи
 * - OLED SSD1306 (I2C: SDA=GPIO8, SCL=GPIO9)
 * - Веб-интерфейс с реальными данными (время, погода, город)
 * - Автоматическое определение города (ip-api.com) или ручной ввод
 * - Погода через wttr.in API
 * - Кнопка 1 (GPIO1): переключение время/погода (короткое), сброс WiFi (долгое 3с)
 * - Кнопка 2 (GPIO2): перезагрузка
 * - Подробная Serial-отладка всех операций
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <Wire.h>
#include <NTPClient.h>
#include <U8g2lib.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <ESPAsyncWebServer.h>

// === КОНФИГУРАЦИЯ ===
// Раскомментируйте строку ниже чтобы ОТКЛЮЧИТЬ OLED и проверить AP
// #define TEST_NO_OLED  true

// OLED дисплей (I2C) — как в РАБОЧЕМ коде
#define OLED_SCL 8   // Clock
#define OLED_SDA 9   // Data
#define OLED_I2C_ADDR 0x3C

#define GEOLOCATION_URL  "http://ip-api.com/json/?fields=city,lat,lon,country"
#define GEO_HOST         "ip-api.com"
#define GEOLOCATION_URL_ALT  "http://ipwhois.app/json/"

// wttr.in — прямой IP (DNS на ESP32-C3 ненадёжен)
#define WTR_HOST    "5.9.243.187"
#define WTR_DOMAIN  "wttr.in"

// Кнопки
#define BUTTON_PIN_1 1  // Короткое: переключение экран, долгое 3с: сброс WiFi
#define BUTTON_PIN_2 2  // Перезагрузка

// Дебаунс кнопок
#define BUTTON_DEBOUNCE_MS 50
#define BUTTON_LONG_PRESS_MS 3000
#define BUTTON_REPEAT_MS 500

// WiFi — временная жёсткая прошивка
const char* WIFI_SSID = "L-1";
const char* WIFI_PASS = "02323763";

// NTP — UTC+3 (Москва)
const long NTP_OFFSET = 10800;  // 3 * 3600
const int NTP_INTERVAL = 60000;

// Инициализация — программный I2C (как в рабочем коде)
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, OLED_SCL, OLED_SDA);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", NTP_OFFSET, NTP_INTERVAL);

Preferences preferences;
AsyncWebServer server(80);

// Глобальные переменные
String currentTime = "";
String currentDate = "";
String city = "";
String weather = "";
String weatherRaw = "";
float geoLat = 0, geoLon = 0;
bool showWeather = false;
bool oledWorking = false;
unsigned long lastNTPSync = 0;
unsigned long lastWeatherFetch = 0;
unsigned long lastButtonLog = 0;

// Состояние кнопки 1
struct ButtonState {
    bool pressed = false;
    bool wasPressed = false;
    unsigned long pressTime = 0;
    bool longHandled = false;
    unsigned long lastRepeat = 0;
};
ButtonState btn1;

// Для обновления OLED раз в секунду (не блокировать WiFi)
unsigned long lastDisplayUpdate = 0;
String prevTime = "", prevDate = "", prevCity = "", prevWeather = "", prevSSID = "";
bool prevShowWeather = false;

// === ФУНКЦИИ ===

void detectCity();
void fetchWeather();

/**
 * Подключение к WiFi с fallback на AP Mode
 * 1. Пробуем STA 20 сек
 * 2. Если не удалось — поднимаем AP с web-сервером настройки
 */
void connectWiFi() {
    // Пробуем загрузить сохранённые креды из Preferences
    preferences.begin("wifi", true);
    String savedSsid = preferences.getString("ssid", "");
    String savedPass = preferences.getString("pass", "");
    preferences.end();

    const char* ssid = WIFI_SSID;
    const char* pass = WIFI_PASS;

    if (savedSsid.length() > 0) {
        ssid = savedSsid.c_str();
        pass = savedPass.c_str();
        Serial.printf("[WiFi] Используем сохранённые креды: '%s'\n", ssid);
    }

    Serial.printf("[WiFi] Подключение к '%s'...\n", ssid);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);

    uint8_t attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {  // 40 * 500мс = 20 сек
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WiFi] ✅ Подключена! IP: %s | RSSI: %d dBm\n",
                      WiFi.localIP().toString().c_str(), WiFi.RSSI());
        detectCity();
        return;
    }

    // Fallback: AP Mode
    Serial.printf("\n[WiFi] ❌ Не удалось подключиться к '%s'\n", ssid);
    Serial.println("[WiFi] 📡 Поднимаю AP Mode (WiFi-Clock-Setup)...");

    WiFi.mode(WIFI_AP);
    bool apStarted = WiFi.softAP("WiFi-Clock-Setup", "12345678");
    if (apStarted) {
        Serial.printf("[WiFi] ✅ AP запущен! IP: %s\n", WiFi.softAPIP().toString().c_str());
        oledWorking = true;  // Разрешаем отрисовку даже если OLED не init
        setupWebServer();    // Веб-сервер для настройки WiFi
    } else {
        Serial.println("[WiFi] ❌ Ошибка запуска AP!");
    }
}

/**
 * Определение города по IP-геолокации
 * Пробует основной сервис (ip-api.com), затем альтернативный (ipwhois.app).
 * Если оба вернули Москву — fallback на захардкоженный город.
 */
void detectCity() {
    Serial.println("[Geo] Определяю город...");

    // --- Попытка 1: ip-api.com ---
    Serial.println("[Geo] Попытка 1: ip-api.com");
    HTTPClient http;
    http.begin(GEOLOCATION_URL);
    http.addHeader("Host", GEO_HOST);
    http.setTimeout(15000);

    int httpCode = http.GET();
    Serial.printf("[Geo] HTTP code: %d\n", httpCode);

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        Serial.printf("[Geo] RAW ответ (ip-api): %s\n", payload.c_str());

        DynamicJsonDocument doc(1024);
        DeserializationError error = deserializeJson(doc, payload);

        if (!error) {
            const char* c = doc["city"];
            if (c && strlen(c) > 0) {
                city = String(c);
                geoLat = doc["lat"].as<float>();
                geoLon = doc["lon"].as<float>();
                Serial.printf("[Geo] 🌍 %s (%.4f, %.4f)\n", city.c_str(), geoLat, geoLon);

                // Если город — Москва, пробуем альтернативу
                // (провайдер может быть зарегистрирован в Москве)
                if (city == "Moscow" || city == "Москва") {
                    Serial.println("[Geo] ⚠️ ip-api вернул Москву, пробую альтернативный сервис...");
                } else {
                    preferences.begin("ntp", false);
                    preferences.putString("city", city);
                    preferences.end();
                    http.end();
                    return;
                }
            }
        } else {
            Serial.printf("[Geo] JSON parse error: %s\n", error.c_str());
        }
    } else {
        Serial.printf("[Geo] HTTP error: %d\n", httpCode);
    }
    http.end();

    // --- Попытка 2: ipwhois.app ---
    Serial.println("[Geo] Попытка 2: ipwhois.app");
    http.begin(GEOLOCATION_URL_ALT);
    http.setTimeout(15000);

    httpCode = http.GET();
    Serial.printf("[Geo] HTTP code: %d\n", httpCode);

    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        Serial.printf("[Geo] RAW ответ (ipwhois): %s\n", payload.c_str());

        // ipwhois.app возвращает большой JSON — нужен больший буфер
        DynamicJsonDocument doc(2048);  // Уменьшено с 4096 для экономии RAM на ESP32-C3
        DeserializationError error = deserializeJson(doc, payload);

        if (!error) {
            const char* c = doc["city"];
            if (c && strlen(c) > 0) {
                city = String(c);
                geoLat = doc["latitude"].as<float>();
                geoLon = doc["longitude"].as<float>();
                Serial.printf("[Geo] 🌍 %s (%.4f, %.4f)\n", city.c_str(), geoLat, geoLon);

                if (city == "Moscow" || city == "Москва") {
                    Serial.println("[Geo] ⚠️ ipwhois тоже вернул Москву. Использую fallback.");
                } else {
                    preferences.begin("ntp", false);
                    preferences.putString("city", city);
                    preferences.end();
                    http.end();
                    return;
                }
            }
        } else {
            Serial.printf("[Geo] JSON parse error: %s\n", error.c_str());
        }
    } else {
        Serial.printf("[Geo] HTTP error: %d\n", httpCode);
    }
    http.end();

    // --- Fallback: захардкоженный город для отладки ---
    // Чтобы отключить fallback, закомментируйте блок ниже:
    if (city.length() == 0 || city == "Moscow" || city == "Москва") {
        Serial.println("[Geo] ⚠️ Не удалось определить город. Fallback: 'Ростов-на-Дону'");
        city = "Ростов-на-Дону";
        geoLat = 47.2357;
        geoLon = 39.7015;
    }

    preferences.begin("ntp", false);
    preferences.putString("city", city);
    preferences.end();
}

/**
 * Получение погоды через wttr.in — JSON API (format=j1)
 * Формат %C с lang=ru ненадёжен (возвращает пустоту), поэтому
 * используем JSON-ответ, где есть и описание, и температура.
 */
void fetchWeather() {
    if (city.length() == 0) {
        Serial.println("[Weather] Город не задан");
        weather = "No city set";
        return;
    }

    // Первый запрос без задержки, далее каждые 5 минут
    unsigned long now = millis();
    if (lastWeatherFetch != 0 && (now - lastWeatherFetch < 300000)) {
        return;
    }
    lastWeatherFetch = now;

    // Запрос погоды — простой текст (не JSON), прямой IP
    // %C = описание, %t = температура
    String urlC = "http://" WTR_HOST "/" + city + "?format=%C&lang=ru";
    String urlT = "http://" WTR_HOST "/" + city + "?format=%t&lang=ru";
    lastWeatherFetch = now;

    HTTPClient http;
    String desc = "", temp = "";

    // Описание
    http.begin(urlC);
    http.addHeader("Host", WTR_DOMAIN);
    http.setUserAgent("WiFi-Clock/1.0");
    http.setTimeout(10000);
    int codeC = http.GET();
    if (codeC == HTTP_CODE_OK) {
        desc = http.getString();
        desc.trim();
    }
    http.end();

    // Температура
    http.begin(urlT);
    http.addHeader("Host", WTR_DOMAIN);
    http.setUserAgent("WiFi-Clock/1.0");
    http.setTimeout(10000);
    int codeT = http.GET();
    if (codeT == HTTP_CODE_OK) {
        temp = http.getString();
        temp.trim();
    }
    http.end();

    Serial.printf("[Weather] Desc(%d): '%s'\n", codeC, desc.c_str());
    Serial.printf("[Weather] Temp(%d): '%s'\n", codeT, temp.c_str());

    if (desc.length() > 0 && temp.length() > 0) {
        weather = desc + "\n" + temp;
    } else if (desc.length() > 0) {
        weather = desc;
    } else if (temp.length() > 0) {
        weather = temp;
    } else {
        weather = "Weather err";
    }
}

/**
 * Обработка кнопок с подробной отладкой
 */
void handleButtons() {
    unsigned long now = millis();

    // === Кнопка 1 (GPIO1) ===
    bool btn1State = digitalRead(BUTTON_PIN_1) == LOW;

    if (btn1State && !btn1.pressed) {
        // Только что нажата
        btn1.pressed = true;
        btn1.pressTime = now;
        btn1.longHandled = false;
        btn1.lastRepeat = now;
        Serial.printf("[Button1] 📌 Нажата (t=%lu)\n", now);
    } else if (btn1State && btn1.pressed) {
        // Удерживается
        unsigned long holdTime = now - btn1.pressTime;

        // Лог каждые 500мс
        if (now - lastButtonLog > 500 && holdTime > 500) {
            Serial.printf("[Button1] Удержание... %lu мс\n", holdTime);
            lastButtonLog = now;
        }

        // Долгое нажатие 3с — сброс WiFi
        if (holdTime > BUTTON_LONG_PRESS_MS && !btn1.longHandled) {
            btn1.longHandled = true;
            Serial.println("[Button1] 🔄 Долгое нажатие! Сброс настроек WiFi...");
            preferences.begin("wifi", false);
            preferences.clear();
            preferences.end();
            Serial.println("[Button1] Настройки WiFi очищены. Перезагрузка...");
            delay(500);
            ESP.restart();
        }
    } else if (!btn1State && btn1.pressed) {
        // Отпущена
        unsigned long holdTime = now - btn1.pressTime;
        btn1.pressed = false;
        Serial.printf("[Button1] Отпущена (удержание: %lu мс)\n", holdTime);

        // Короткое нажатие — переключение экрана
        if (holdTime > BUTTON_DEBOUNCE_MS && holdTime < BUTTON_LONG_PRESS_MS) {
            showWeather = !showWeather;
            Serial.printf("[Button1] 🔄 Переключение: %s\n", showWeather ? "ПОГОДА" : "ВРЕМЯ");
        }
    }

    // === Кнопка 2 (GPIO2) — перезагрузка ===
    static bool btn2Pressed = false;
    static unsigned long btn2Time = 0;
    bool btn2State = digitalRead(BUTTON_PIN_2) == LOW;

    if (btn2State && !btn2Pressed) {
        btn2Pressed = true;
        btn2Time = now;
        Serial.printf("[Button2] 📌 Нажата (t=%lu)\n", now);
    } else if (!btn2State && btn2Pressed) {
        unsigned long holdTime = now - btn2Time;
        btn2Pressed = false;
        Serial.printf("[Button2] Отпущена (удержание: %lu мс)\n", holdTime);

        if (holdTime > BUTTON_DEBOUNCE_MS) {
            Serial.println("[Button2] 🔄 Перезагрузка по кнопке...");
            delay(200);
            ESP.restart();
        }
    }
}

/**
 * Обновление времени
 */
void updateTime() {
    if (WiFi.status() != WL_CONNECTED) return;

    unsigned long now = millis();

    // Первичная синхронизация
    if (lastNTPSync == 0) {
        Serial.println("[NTP] Первичная синхронизация...");
        timeClient.forceUpdate();
        lastNTPSync = now;
    }
    // Периодическое обновление
    else if (now - lastNTPSync > NTP_INTERVAL) {
        Serial.println("[NTP] Обновление времени...");
        timeClient.update();
        lastNTPSync = now;
    }

    // Форматируем время — только HH:MM
    char timeBuf[16];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", timeClient.getHours(), timeClient.getMinutes());
    currentTime = String(timeBuf);

    // Форматируем дату
    time_t epoch = timeClient.getEpochTime();
    struct tm* timeinfo = localtime(&epoch);
    char dateBuf[30];
    const char* months[] = {"Янв","Фев","Мар","Апр","Май","Июн",
                            "Июл","Авг","Сен","Окт","Ноя","Дек"};
    snprintf(dateBuf, sizeof(dateBuf), "%02d %s %04d",
             timeinfo->tm_mday, months[timeinfo->tm_mon], timeinfo->tm_year + 1900);
    currentDate = String(dateBuf);
}

/**
 * Отрисовка на OLED
 */
void displayUpdate() {
    if (!oledWorking) return;

    u8g2.firstPage();
    do {
        if (showWeather && weather.length() > 0) {
            // Экран погоды
            int nl = weather.indexOf('\n');
            String desc = (nl > 0) ? weather.substring(0, nl) : weather;
            String temp = (nl > 0) ? weather.substring(nl + 1) : "";

            // Город (по центру, кириллица)
            u8g2.setFont(u8g2_font_6x13_t_cyrillic);
            if (city.length() > 0) {
                int cx = (128 - u8g2.getStrWidth(city.c_str())) / 2;
                u8g2.drawStr(cx, 14, city.c_str());
            }

            // Описание погоды (по центру, кириллица)
            u8g2.setFont(u8g2_font_7x13_t_cyrillic);
            int dw = u8g2.getStrWidth(desc.c_str());
            if (dw > 120) {
                u8g2.setFont(u8g2_font_6x13_t_cyrillic);
                dw = u8g2.getStrWidth(desc.c_str());
            }
            u8g2.drawStr((128 - dw) / 2, 40, desc.c_str());

            // Температура (по центру, крупные цифры)
            if (temp.length() > 0) {
                u8g2.setFont(u8g2_font_logisoso22_tr);
                int tw = u8g2.getStrWidth(temp.c_str());
                u8g2.drawStr((128 - tw) / 2, 62, temp.c_str());
            }
        } else if (currentTime.length() > 0) {
            // Экран часов
            // В page mode clearBuffer() не работает! Рисуем чёрный фон:
            u8g2.setDrawColor(0);
            u8g2.drawBox(0, 0, 128, 64);
            u8g2.setDrawColor(1);

            // WiFi SSID (вверху, по центру)
            if (WiFi.status() == WL_CONNECTED) {
                String ssid = WiFi.SSID();
                if (ssid.length() > 16) ssid = ssid.substring(0, 15) + ".";
                u8g2.setFont(u8g2_font_5x7_tr);
                int sx = (128 - u8g2.getStrWidth(ssid.c_str())) / 2;
                u8g2.drawStr(sx, 10, ssid.c_str());
            }

            // Время HH:MM (по центру, крупно)
            u8g2.setFont(u8g2_font_inb24_mn);
            int tw = u8g2.getStrWidth(currentTime.c_str());
            u8g2.drawStr((128 - tw) / 2, 42, currentTime.c_str());

            // Дата (внизу, по центру, кириллица)
            u8g2.setFont(u8g2_font_6x13_t_cyrillic);
            int dw = u8g2.getStrWidth(currentDate.c_str());
            u8g2.drawStr((128 - dw) / 2, 62, currentDate.c_str());
        } else {
            // NTP ещё не получен — показываем статус
            // Очистка в page mode
            u8g2.setDrawColor(0);
            u8g2.drawBox(0, 0, 128, 64);
            u8g2.setDrawColor(1);

            u8g2.setFont(u8g2_font_6x13_t_cyrillic);
            if (WiFi.status() == WL_CONNECTED) {
                u8g2.drawStr(5, 15, "NTP sync...");
            } else {
                u8g2.drawStr(5, 15, "WiFi: AP Mode");
                u8g2.drawStr(5, 30, "Setup: 192.168.4.1");
                u8g2.drawStr(5, 50, "Connect to:");
                u8g2.setFont(u8g2_font_5x7_tr);
                u8g2.drawStr(5, 62, "WiFi-Clock-Setup / 12345678");
            }
        }
    } while (u8g2.nextPage());
}

/**
 * Веб-сервер — МИНИМАЛЬНЫЙ для AP режима
 * Только форма: SSID + пароль → сохранить
 */
void setupWebServer() {

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
        String html = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>WiFi Clock Setup</title>
<style>
body{font-family:sans-serif;background:#1a1a2e;color:#eee;text-align:center;padding:20px}
input{width:90%;padding:10px;margin:8px 0;border-radius:6px;border:1px solid #0f3460;background:#0f3460;color:#eee;font-size:16px}
button{width:95%;padding:12px;background:#00d4ff;color:#1a1a2e;border:none;border-radius:6px;font-size:16px;font-weight:bold;margin:5px 0;cursor:pointer}
button.d{background:#e94560;color:#fff}
</style></head><body>
<h2>⏰ WiFi Clock Setup</h2>
<form action="/save" method="POST">
<input name="ssid" placeholder="WiFi SSID" required>
<input name="pass" placeholder="WiFi Password" type="password">
<button type="submit">💾 Сохранить WiFi</button>
</form>
<form action="/reset" method="POST"><button type="submit" class="d">🗑 Сброс настроек</button></form>
</body></html>)rawliteral";
        request->send(200, "text/html", html);
    });

    server.on("/save", HTTP_POST, [](AsyncWebServerRequest *request) {
        if (request->hasParam("ssid", true)) {
            String ssid = request->getParam("ssid", true)->value();
            String pass = request->hasParam("pass", true) ?
                          request->getParam("pass", true)->value() : "";
            Serial.printf("[Web] Сохранение WiFi: '%s'\n", ssid.c_str());
            preferences.begin("wifi", false);
            preferences.putString("ssid", ssid);
            preferences.putString("pass", pass);
            preferences.end();
            request->send(200, "text/html",
                "<!DOCTYPE html><html><body style='background:#1a1a2e;color:#eee;text-align:center;padding:40px;font-family:sans-serif;'>"
                "<h2>✅ Сохранено! Перезагрузка...</h2></body></html>");
            delay(500);
            ESP.restart();
        }
    });

    server.on("/reset", HTTP_POST, [](AsyncWebServerRequest *request) {
        Serial.println("[Web] Сброс настроек WiFi");
        preferences.begin("wifi", false);
        preferences.clear();
        preferences.end();
        request->send(200, "text/html",
            "<!DOCTYPE html><html><body style='background:#1a1a2e;color:#eee;text-align:center;padding:40px;font-family:sans-serif;'>"
            "<h2>🗑 Сброшено! Перезагрузка...</h2></body></html>");
        delay(500);
        ESP.restart();
    });

    // API статус (для отладки)
    server.on("/st", HTTP_GET, [](AsyncWebServerRequest *request) {
        request->send(200, "text/plain",
            "time:" + currentTime + "|date:" + currentDate +
            "|city:" + city + "|weather:" + weather +
            "|wifi:" + String(WiFi.status() == WL_CONNECTED ? "ok" : "ap") +
            "|ip:" + (WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString()) +
            "|ram:" + String(ESP.getFreeHeap()));
    });

    server.begin();
    Serial.println("[Web] Веб-сервер запущен (минимальный)");
}

// === SETUP & LOOP ===

void setup() {
    Serial.begin(115200);
    Serial.println("\n======================================");
    Serial.println("  WiFi Clock OLED — ESP32-C3");
    Serial.println("======================================\n");

    // Кнопки
    pinMode(BUTTON_PIN_1, INPUT_PULLUP);
    pinMode(BUTTON_PIN_2, INPUT_PULLUP);
    Serial.printf("[Setup] Кнопки: BTN1=GPIO%d, BTN2=GPIO%d\n", BUTTON_PIN_1, BUTTON_PIN_2);

#ifndef TEST_NO_OLED
    // OLED — инициализация
    Serial.println("\n[Setup] --- OLED ---");
    Serial.printf("[Setup] SDA=GPIO%d, SCL=GPIO%d (SW I2C), адрес=0x%02X\n", OLED_SDA, OLED_SCL, OLED_I2C_ADDR);
    oledWorking = u8g2.begin();

    if (oledWorking) {
        Serial.println("[Setup] ✅ OLED begin() вернул true");
        u8g2.firstPage();
        do {
            u8g2.setFont(u8g2_font_ncenB14_tr);
            u8g2.drawStr(5, 20, "WiFi Clock");
            u8g2.setFont(u8g2_font_6x12_tr);
            u8g2.drawStr(5, 40, "ESP32-C3");
            u8g2.drawStr(5, 55, "Ready!");
        } while (u8g2.nextPage());
    } else {
        Serial.println("[Setup] ❌ OLED begin() вернул false!");
        Serial.println("[Setup] Проверьте:");
        Serial.println("[Setup]   1. Подключение SDA→GPIO9, SCL→GPIO8");
        Serial.println("[Setup]   2. Питание OLED 3.3V");
        Serial.println("[Setup]   3. Тип дисплея SSD1306 128x64");
    }
#else
    Serial.println("\n[Setup] *** OLED ОТКЛЮЧЕН (TEST_NO_OLED) ***");
    oledWorking = false;
#endif

    // WiFi
    Serial.println("\n[Setup] --- WiFi ---");
    connectWiFi();

    // NTP
    Serial.println("\n[Setup] --- NTP ---");
    timeClient.begin();

    // Веб-сервер запускается автоматически в connectWiFi() при AP Mode

    Serial.println("\n======================================");
    Serial.println("  ✅ Система готова!");
    Serial.println("======================================\n");
}

unsigned long lastLog = 0;

void loop() {
    handleButtons();
    updateTime();

    unsigned long now = millis();

    // Авто-определение города (если WiFi есть, но город ещё не определён)
    if (WiFi.status() == WL_CONNECTED && city.length() == 0) {
        detectCity();
    }

    // Авто-запрос погоды: первый запрос через 30 сек после старта, далее каждые 5 мин
    if (WiFi.status() == WL_CONNECTED && city.length() > 0) {
        if (lastWeatherFetch == 0 && now > 30000) {
            // Первый запрос через 30 сек
            fetchWeather();
        } else if (now - lastWeatherFetch >= 300000) {
            // Далее каждые 5 мин
            fetchWeather();
        }
    }

    // Лог каждые 10 секунд
    if (now - lastLog > 10000) {
        lastLog = now;
        Serial.printf("[Status] ⏰ %s | 📅 %s | 🌍 %s | 🌤 %s | 📶 %s | RAM: %d\n",
                      currentTime.c_str(),
                      currentDate.c_str(),
                      city.c_str(),
                      weather.length() > 0 ? weather.c_str() : "(нет данных)",
                      WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString().c_str() : "AP_MODE",
                      ESP.getFreeHeap());
    }

    // Обновляем OLED ТОЛЬКО если данные изменились ИЛИ прошла 1 секунда
    bool dataChanged = (currentTime != prevTime || currentDate != prevDate ||
                        city != prevCity || weather != prevWeather ||
                        showWeather != prevShowWeather ||
                        WiFi.SSID() != prevSSID);

#ifndef TEST_NO_OLED
    if (oledWorking && (dataChanged || (now - lastDisplayUpdate >= 1000))) {
        displayUpdate();
        lastDisplayUpdate = now;
        prevTime = currentTime;
        prevDate = currentDate;
        prevCity = city;
        prevWeather = weather;
        prevShowWeather = showWeather;
        prevSSID = WiFi.SSID();
    }
#endif

    delay(100);
}
