/**
 * УМНЫЙ КОТЕЛ v4.2 (РЕФАКТОРИНГ)
 *
 * Модульная архитектура:
 * - config.h       — настройки, пины, константы
 * - sensors.h/cpp  — чтение NTC, проверка безопасности
 * - control.h/cpp  — Брезенхем, авто-мощность, реле
 * - telegram.h/cpp — Telegram бот, очередь, пользователи
 * - webserver.h/cpp — веб-сервер, API endpoints
 * - storage.h/cpp  — Preferences (настройки, пользователи)
 *
 * Особенности:
 * - Подключение к существующей Wi-Fi сети
 * - При потере Wi-Fi — перезагрузка
 * - Миллисекундный алгоритм Брезенхема (10 Гц)
 * - Ограничение мощности в авто режиме
 * - Режим "Не заморозить" от 10°C
 * - Стабильное состояние реле с защитой от дребезга
 * - Точный расчёт потребления для 4кВт котла
 */

#include <WiFi.h>
#include <LittleFS.h>
#include "config.h"
#include "sensors.h"
#include "control.h"
#include "telegram.h"
#include "webserver.h"
#include "storage.h"

// === ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ИСТОРИИ ===
float tempHistory[HISTORY_SIZE] = { 0 };
float flowHistory[HISTORY_SIZE] = { 0 };
float airHistory[HISTORY_SIZE] = { 0 };
int powerHistory[HISTORY_SIZE] = { 0 };
unsigned long historyTimestamps[HISTORY_SIZE] = { 0 };
int historyIndex = 0;

// === АВТОНОМНЫЙ РЕЖИМ ===
bool autonomousMode = false;
unsigned long lastNetworkCheck = 0;
int currentWifiIndex = 0;  // 0 = первая сеть, 1 = вторая

// Массив WiFi сетей (порядок = приоритет) — extern, доступен в других модулях
const WifiConfig wifiNetworks[] = {
    { WIFI_SSID_1, WIFI_PASS_1 },
    { WIFI_SSID_2, WIFI_PASS_2 },
};
const int wifiNetworkCount = (sizeof(wifiNetworks) / sizeof(wifiNetworks[0]));

// === ПОДКЛЮЧЕНИЕ К WI-FI С ПЕРЕБОРОМ СЕТЕЙ ===
// Возвращает true если подключились, false если ни одна сеть недоступна
bool connectToWifi() {
    WiFi.mode(WIFI_STA);

    for (int i = 0; i < wifiNetworkCount; i++) {
        // Пропускаем пустые SSID
        if (strlen(wifiNetworks[i].ssid) == 0 || strcmp(wifiNetworks[i].ssid, "YOUR_WIFI_SSID_1") == 0) {
            continue;
        }

        Serial.printf("\n📡 Попытка подключения к сети #%d: '%s'...\n", i + 1, wifiNetworks[i].ssid);

        WiFi.disconnect(false);
        delay(100);

        for (int attempt = 0; attempt < WIFI_MAX_ATTEMPTS; attempt++) {
            if (attempt > 0) {
                Serial.printf("  Повторная попытка %d/%d...\n", attempt + 1, WIFI_MAX_ATTEMPTS);
                WiFi.disconnect(false);
                delay(500);
            }

            WiFi.begin(wifiNetworks[i].ssid, wifiNetworks[i].password);

            unsigned long startWait = millis();
            while (millis() - startWait < WIFI_CONNECT_TIMEOUT) {
                if (WiFi.status() == WL_CONNECTED) {
                    Serial.printf("✅ WiFi подключён: '%s' (IP: %s, RSSI: %d dBm)\n",
                                 wifiNetworks[i].ssid,
                                 WiFi.localIP().toString().c_str(),
                                 WiFi.RSSI());
                    currentWifiIndex = i;
                    return true;
                }
                delay(500);
                Serial.print(".");
            }
            Serial.println();
        }
        Serial.printf("❌ Не удалось подключиться к '%s'\n", wifiNetworks[i].ssid);
        WiFi.disconnect(false);
    }

    Serial.println("❌ Ни одна WiFi сеть недоступна");
    return false;
}

// === ПРОВЕРКА И ПЕРЕКЛЮЧЕНИЕ СЕТИ ===
bool checkNetworkAvailable() {
    if (WiFi.status() == WL_CONNECTED) return true;

    Serial.println("🔍 Сеть потеряна. Пробуем переподключение...");

    // Сначала пробуем текущую сеть
    if (currentWifiIndex < wifiNetworkCount && strlen(wifiNetworks[currentWifiIndex].ssid) > 0) {
        Serial.printf("  Переподключение к текущей сети: '%s'...\n", wifiNetworks[currentWifiIndex].ssid);
        WiFi.begin(wifiNetworks[currentWifiIndex].ssid, wifiNetworks[currentWifiIndex].password);

        unsigned long startWait = millis();
        while (millis() - startWait < WIFI_CONNECT_TIMEOUT) {
            if (WiFi.status() == WL_CONNECTED) {
                Serial.println("✅ Переподключение успешно!");
                return true;
            }
            delay(500);
        }
    }

    // Если не получилось — пробуем другие сети
    Serial.println("  Текущая сеть недоступна, пробуем резервную...");
    WiFi.disconnect(false);
    delay(100);

    // Временно меняем currentWifiIndex чтобы connectToWifi пропустил текущую
    int savedIndex = currentWifiIndex;
    currentWifiIndex = wifiNetworkCount;  // "подключаемся" к несуществующей сети

    if (connectToWifi()) {
        return true;
    }

    currentWifiIndex = savedIndex;
    Serial.println("❌ Ни одна сеть недоступна");
    return false;
}

// === ОПРОС ДАТЧИКОВ И УПРАВЛЕНИЕ ===
void updateSensorsAndControl() {
    readAllSensors();
    bool safe = checkSafety(currentFlowTemp, currentWaterOk);

    if (!safe) {
        setPower(0);
        setRelay(false);

        // Автоматическое отключение ручного режима при опасности
        if (currentMode == MODE_MANUAL) {
            currentMode = MODE_OFF;
            saveSettings();
            String alert = "🔥 *АВАРИЙНОЕ ОТКЛЮЧЕНИЕ*\n";
            alert += "Ручной режим принудительно отключен из-за угрозы перегрева!\n";
            alert += "Температура верхней точки: " + String(currentFlowTemp, 1) + "°C";
            notifyAll(alert);
        }
        return;
    }

    // Основная автоматика по нижней точке (вход в котёл)
    if (currentMode == MODE_MAINTAIN) {
        int newPower = calculateAutoPower(currentReturnTemp, currentFlowTemp);
        if (newPower != powerTarget) {
            setPower(newPower);
        }
    }

    // Сохранение истории
    static unsigned long lastHist = 0;
    if (millis() - lastHist > GRAPH_UPDATE_INTERVAL) {
        lastHist = millis();
        tempHistory[historyIndex] = currentReturnTemp;
        flowHistory[historyIndex] = currentFlowTemp;
        airHistory[historyIndex] = currentAirTemp;
        powerHistory[historyIndex] = powerTarget;
        historyTimestamps[historyIndex] = millis();
        historyIndex = (historyIndex + 1) % HISTORY_SIZE;
    }

    // Отладочный вывод
    static unsigned long lastDebug = 0;
    if (millis() - lastDebug > 60000UL) {
        lastDebug = millis();
        Serial.printf("[Автоматика] Обратка: %.1f°C | Подача: %.1f°C | Режим: %s | Мощность: %d%%\n",
                     currentReturnTemp, currentFlowTemp, getModeString().c_str(), powerTarget);
    }
}

// ==================== SETUP ====================
void setup() {
    Serial.begin(115200);
    delay(2000);

    Serial.println("\n=== УМНЫЙ КОТЕЛ v4.2 (РЕФАКТОРИНГ) ===");
    Serial.println("⚡ Автономная работа при отсутствии сети");
    Serial.println("🎯 Температура от " + String(MIN_TARGET_TEMP) + "°C");
    Serial.println("🔒 Авто режим: макс " + String(MAX_POWER_AUTO) + "%");
    Serial.println("💧 Стабильное состояние реле");

    // Инициализация очереди
    messageQueueHead = 0;
    messageQueueTail = 0;
    messageQueueCount = 0;
    Serial.println("✅ Очередь сообщений очищена");

    // LittleFS
    if (!LittleFS.begin()) {
        Serial.println("❌ Ошибка LittleFS!");
    } else {
        Serial.println("✅ LittleFS инициализирован");
    }

    // Инициализация модулей
    initControl();
    initSensors();
    loadSettings();
    loadUsers();
    initTelegram();

    // WiFi подключение с перебором сетей
    if (connectToWifi()) {
        Serial.println("\n✅ WiFi подключен: " + WiFi.localIP().toString());
        initWebServer();
        autonomousMode = false;
        if (userCount > 0) notifyOwnerOnStartup();
    } else {
        Serial.println("\n⚠️ WiFi не подключён. Работаем автономно.");
        autonomousMode = true;
        webServerEnabled = false;
        webServer.stop();
    }

    digitalWrite(LED_PIN, LOW);
    Serial.println("✅ Система готова к работе");
}

// ==================== LOOP ====================
void loop() {
    static unsigned long lastSensor = 0, lastTelegramCheck = 0;
    unsigned long now = millis();

    // Telegram (только если есть сеть)
    if (!autonomousMode && WiFi.status() == WL_CONNECTED) {
        if (now - lastTelegramCheck >= TELEGRAM_INTERVAL) {
            lastTelegramCheck = now;
            checkTelegramMessages();
        }
        processTelegramQueue();
    }

    // Веб-сервер (только если есть сеть)
    if (!autonomousMode && webServerEnabled) {
        webServer.handleClient();
    }

    // Управление реле работает ВСЕГДА
    updateBresenham();

    // Датчики работают ВСЕГДА
    if (now - lastSensor > SENSOR_INTERVAL) {
        lastSensor = now;
        updateSensorsAndControl();
    }

    // Проверка сети в автономном режиме
    if (autonomousMode) {
        if (now - lastNetworkCheck > NETWORK_CHECK_INTERVAL) {
            lastNetworkCheck = now;
            Serial.println("🔄 Проверка сети (автономный режим)...");

            if (checkNetworkAvailable()) {
                Serial.println("✅ Сеть найдена! Перезагрузка для подключения...");
                delay(2000);
                ESP.restart();
            } else {
                Serial.println("❌ Сеть не найдена. Продолжаем автономно.");
            }
        }
    }

    // LED индикация
    static unsigned long lastBlink = 0;
    if (now - lastBlink > 1000UL) {
        lastBlink = now;
        digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    }

    delay(1);
}
