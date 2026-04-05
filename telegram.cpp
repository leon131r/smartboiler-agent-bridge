/**
 * telegram.cpp — Telegram бот, очередь сообщений, пользователи
 */

#include "telegram.h"
#include "config.h"
#include "sensors.h"
#include "control.h"
#include "storage.h"
#include <ArduinoJson.h>

// Глобальные объекты
WiFiClientSecure telegramClient;
UniversalTelegramBot* bot = nullptr;

// Пользователи
User users[MAX_USERS];
int userCount = 0;

// Очередь сообщений
TelegramMessage messageQueue[MESSAGE_QUEUE_SIZE];
int messageQueueHead = 0;
int messageQueueTail = 0;
int messageQueueCount = 0;

// Кэш обработанных сообщений
ProcessedMessage processedCache[PROCESSED_CACHE_SIZE];
int processedCacheCount = 0;

// Автономный режим — declarations в telegram.h
String getCurrentWifiSsid() {
    if (currentWifiIndex >= 0 && currentWifiIndex < wifiNetworkCount) {
        return String(wifiNetworks[currentWifiIndex].ssid);
    }
    return "Нет подключения";
}

// Внешние переменные
extern Mode currentMode;
extern int powerTarget;
extern int targetReturnTemp;
extern int maxFlowTemp;
extern bool reportedRelayState;
extern float currentReturnTemp;
extern float currentFlowTemp;
extern float currentAirTemp;
extern bool currentWaterOk;
extern bool safetyBlock;
extern String safetyReason;
extern String lastError;

void initTelegram() {
    bot = new UniversalTelegramBot(BOT_TOKEN, telegramClient);
    telegramClient.setInsecure();
}

// ==================== ОЧЕРЕДЬ СООБЩЕНИЙ ====================

bool isMessageProcessed(long msgId) {
    unsigned long now = millis();

    // Удаляем записи старше 15 секунд
    for (int i = 0; i < processedCacheCount;) {
        if (now - processedCache[i].receivedAt > 15000UL) {
            for (int j = i; j < processedCacheCount - 1; j++) {
                processedCache[j] = processedCache[j + 1];
            }
            processedCacheCount--;
        } else {
            i++;
        }
    }

    // Проверяем наличие
    for (int i = 0; i < processedCacheCount; i++) {
        if (processedCache[i].messageId == msgId) return true;
    }

    // Добавляем новое
    if (processedCacheCount < PROCESSED_CACHE_SIZE) {
        processedCache[processedCacheCount].messageId = msgId;
        processedCache[processedCacheCount].receivedAt = now;
        processedCacheCount++;
    }

    return false;
}

void sendTelegramMessage(String chat_id, String text, String parse_mode) {
    if (autonomousMode) return;
    if (messageQueueCount >= MESSAGE_QUEUE_SIZE) {
        Serial.println("⚠️ Очередь сообщений переполнена!");
        return;
    }
    messageQueue[messageQueueTail].chat_id = chat_id;
    messageQueue[messageQueueTail].text = text;
    messageQueue[messageQueueTail].parse_mode = parse_mode;
    messageQueueTail = (messageQueueTail + 1) % MESSAGE_QUEUE_SIZE;
    messageQueueCount++;
}

void processTelegramQueue() {
    if (autonomousMode || messageQueueCount == 0 || bot == nullptr) return;

    static unsigned long lastSendTime = 0;
    unsigned long now = millis();
    if (now - lastSendTime < 500UL) return;

    TelegramMessage msg = messageQueue[messageQueueHead];
    try {
        bool success = bot->sendMessage(msg.chat_id, msg.text, msg.parse_mode);
        if (success) {
            messageQueueHead = (messageQueueHead + 1) % MESSAGE_QUEUE_SIZE;
            messageQueueCount--;
            lastSendTime = now;
        } else {
            Serial.println("[QUEUE ERROR] Failed to send");
            lastSendTime = now + 2000UL;
        }
    } catch (...) {
        Serial.println("[QUEUE EXCEPTION] Error");
        lastSendTime = now + 2000UL;
    }
}

void cleanupMessageQueue() {
    static unsigned long lastCleanup = 0;
    unsigned long now = millis();
    if (now - lastCleanup > 30000UL && messageQueueCount > 10) {
        messageQueueHead = messageQueueTail;
        messageQueueCount = 0;
        lastCleanup = now;
    }
}

// ==================== ПОЛЬЗОВАТЕЛИ ====================

UserLevel getUserLevel(String chat_id) {
    for (int i = 0; i < userCount; i++) {
        if (users[i].chat_id == chat_id) return users[i].level;
    }
    return LEVEL_NONE;
}

String getUserName(String chat_id) {
    for (int i = 0; i < userCount; i++) {
        if (users[i].chat_id == chat_id) return users[i].name;
    }
    return "Неизвестный";
}

String getLevelString(UserLevel level) {
    switch (level) {
        case LEVEL_NONE: return "Нет доступа";
        case LEVEL_OBSERVER: return "👁 Наблюдатель";
        case LEVEL_USER: return "👤 Пользователь";
        case LEVEL_OWNER: return "👑 Владелец";
        default: return "Неизвестно";
    }
}

// addUser, removeUser реализованы в storage.cpp

// ==================== УВЕДОМЛЕНИЯ ====================

void notifyAll(String message) {
    if (autonomousMode) {
        Serial.println("📢 Уведомление (автономно): " + message);
        return;
    }
    for (int i = 0; i < userCount; i++) {
        if (users[i].level >= LEVEL_OBSERVER) {
            String msg = "📢 *Уведомление:*\n" + message;
            sendTelegramMessage(users[i].chat_id, msg, "Markdown");
        }
    }
}

void sendAlertToAll(const String& alert) {
    if (autonomousMode) {
        Serial.println("🔔 Алерт (автономно): " + alert);
        return;
    }

    if (alert.indexOf("НИЗКИЙ УРОВЕНЬ ВОДЫ") >= 0 || alert.indexOf("⚠️") >= 0) {
        int sentCount = 0;
        for (int i = 0; i < userCount && sentCount < 5; i++) {
            if (users[i].level >= LEVEL_OBSERVER) {
                sendTelegramMessage(users[i].chat_id, alert, "Markdown");
                sentCount++;
                delay(10);
            }
        }
    }
}

void notifyOwnerOnStartup() {
    for (int i = 0; i < userCount; i++) {
        if (users[i].level == LEVEL_OWNER) {
            String ip = WiFi.localIP().toString();
            String msg = "🔄 *Котёл запущен! v4.2*\n";
            msg += "⚡ Миллисекундный Брезенхем (10 Гц)\n";
            msg += "🎯 Температура от " + String(MIN_TARGET_TEMP) + "°C\n";
            msg += "🔒 Авто режим: макс " + String(MAX_POWER_AUTO) + "%\n";
            msg += "📶 WiFi: *" + getCurrentWifiSsid() + "*\n";
            msg += "📍 IP: `" + ip + "`\n";
            msg += "⏱ Время: " + getTimeString() + "\n";
            msg += "👥 Пользователей: " + String(userCount) + "\n";
            msg += "Отправьте /menu для управления";
            sendTelegramMessage(users[i].chat_id, msg, "Markdown");
            break;
        }
    }
}

// ==================== ВРЕМЯ ====================

String getTimeString() {
    unsigned long sec = millis() / 1000;
    unsigned long min = sec / 60;
    unsigned long hr = min / 60;
    sec %= 60; min %= 60; hr %= 24;
    char buf[20];
    sprintf(buf, "%02lu:%02lu:%02lu", hr, min, sec);
    return String(buf);
}

String getUptimeString() {
    unsigned long sec = millis() / 1000;
    unsigned long min = sec / 60;
    unsigned long hr = min / 60;
    unsigned long days = hr / 24;
    sec %= 60; min %= 60; hr %= 24;
    char buf[50];
    if (days > 0) sprintf(buf, "%luд %02lu:%02lu:%02lu", days, hr, min, sec);
    else sprintf(buf, "%02lu:%02lu:%02lu", hr, min, sec);
    return String(buf);
}

// ==================== ОТПРАВКА ДАННЫХ ====================

void sendStatusToUser(String chat_id) {
    String msg = "🔥 *СТАТУС КОТЛА v4.1*\n";
    msg += "*Управление:*\n";
    msg += "Режим: " + getModeString() + "\n";
    msg += "Цель: *" + String(targetReturnTemp) + "°C*\n";
    msg += (targetReturnTemp <= 15 ? "❄️ Режим \"Не заморозить\"\n" :
            targetReturnTemp <= 25 ? "💎 Экономный режим\n" : "🔥 Нормальный режим\n");
    msg += "Макс: *" + String(maxFlowTemp) + "°C*\n";
    msg += "Мощность: *" + String(powerTarget) + "%*\n";
    msg += "Реле: " + String(reportedRelayState ? "✅ ВКЛ" : "❌ ВЫКЛ") + "\n";
    msg += "*Температуры:*\n";
    msg += "Обратка: *" + String(currentReturnTemp, 1) + "°C*\n";
    msg += "Подача: *" + String(currentFlowTemp, 1) + "°C*\n";
    msg += "Воздух: *" + String(currentAirTemp, 1) + "°C*\n";
    msg += "ΔT: *" + String(currentFlowTemp - currentReturnTemp, 1) + "°C*\n";

    float consumption = 4.0f * powerTarget / 100.0f;
    msg += "*Потребление:*\n";
    msg += "Мгновенное: *" + String(consumption, 1) + " кВт*\n";
    msg += "(" + String(consumption * 1000, 0) + " Вт)\n";

    msg += "*Система:*\n";
    msg += "Вода: " + String(currentWaterOk ? "✅ НОРМА" : "❌ НИЗКИЙ") + "\n";
    msg += "Безопасность: " + String(safetyBlock ? "❌ " + safetyReason : "✅ ОК") + "\n";
    msg += "Время: " + getUptimeString();

    String ip = WiFi.localIP().toString();
    msg += "\n🌐 *Веб-интерфейс:*\nhttp://" + ip;
    sendTelegramMessage(chat_id, msg, "Markdown");
}

void sendTemperaturesToUser(String chat_id) {
    String msg = "🌡 *ТЕМПЕРАТУРЫ*\n";
    msg += "Обратка: *" + String(currentReturnTemp, 1) + "°C*\n";
    msg += "Подача: *" + String(currentFlowTemp, 1) + "°C*\n";
    msg += "Воздух: *" + String(currentAirTemp, 1) + "°C*\n";
    msg += "ΔT: *" + String(currentFlowTemp - currentReturnTemp, 1) + "°C*\n";
    msg += "Вода: " + String(currentWaterOk ? "✅ НОРМА" : "❌ НИЗКИЙ");
    if (currentFlowTemp > maxFlowTemp - 10) {
        msg += "\n⚠️ *Близко к максимуму!*";
    }
    sendTelegramMessage(chat_id, msg, "Markdown");
}

void sendAllCommands(String chat_id) {
    UserLevel level = getUserLevel(chat_id);
    String help = "📋 *КОМАНДЫ УПРАВЛЕНИЯ v4.1*\n";
    if (level >= LEVEL_OBSERVER) {
        help += "*Просмотр:*\n";
        help += "/status - статус системы\n";
        help += "/temp - температуры\n";
        help += "/water - уровень воды\n";
    }
    if (level >= LEVEL_USER) {
        help += "\n*Управление:*\n";
        help += "/on - авто режим (макс " + String(MAX_POWER_AUTO) + "%)\n";
        help += "/off - выключить котёл\n";
        help += "/manual - ручной режим\n";
        help += "/tempXX - цель XX°C (" + String(MIN_TARGET_TEMP) + "-" + String(MAX_TARGET_TEMP) + ")\n";
        help += "/maxXX - макс XX°C (60-90)\n";
        help += "/powerXX - мощность XX% (0-100)\n";
        help += "\n*Примеры:*\n";
        help += "/temp10 - режим \"Не заморозить\"\n";
        help += "/temp15 - экономичный режим\n";
        help += "/temp40 - нормальный режим\n";
    }
    if (level >= LEVEL_OWNER) {
        help += "\n*Администрирование:*\n";
        help += "/users - список пользователей\n";
        help += "/adduser - добавить пользователя\n";
        help += "/removeuser - удалить пользователя\n";
        help += "/reset - сброс ошибок\n";
        help += "/restart - перезагрузка\n";
    }
    help += "\n*Общее:*\n";
    help += "/help - эта справка\n";
    help += "/info - информация о системе";
    sendTelegramMessage(chat_id, help, "Markdown");
}

void sendMainMenu(String chat_id, String user_name, UserLevel level) {
    String message = "🎛 *Управление котлом v4.1*\n";
    message += "👤 " + user_name + "\n";
    message += "📊 Уровень: " + getLevelString(level) + "\n";
    message += "*Новые возможности:*\n";
    message += "⚡ Миллисекундное управление\n";
    message += "🎯 Температура от " + String(MIN_TARGET_TEMP) + "°C\n";
    message += "🔒 Авто режим: макс " + String(MAX_POWER_AUTO) + "%\n";
    message += "Используйте команды:\n";
    message += "🔥 *Статус:*\n`/status` `/temp` `/water`\n";
    message += "⚙️ *Управление:*\n`/on` `/off` `/manual`\n";
    message += "`/temp10` `/temp15` `/temp20` `/temp40`\n";
    message += "`/power25` `/power50` `/power75`\n";
    String ip = WiFi.localIP().toString();
    message += "🌐 *Веб-интерфейс:*\nhttp://" + ip;
    sendTelegramMessage(chat_id, message, "Markdown");
}

// ==================== ОБРАБОТКА СООБЩЕНИЙ ====================

void checkTelegramMessages() {
    if (autonomousMode || bot == nullptr) return;

    try {
        int numNewMessages = bot->getUpdates(bot->last_message_received + 1);
        if (numNewMessages > 0) {
            Serial.printf("📱 Получено %d новых сообщений\n", numNewMessages);

            int toProcess = min(numNewMessages, 5);
            for (int i = 0; i < toProcess; i++) {
                long messageId = bot->messages[i].message_id;
                String text = bot->messages[i].text;
                String chat_id = String(bot->messages[i].chat_id);
                String from_name = bot->messages[i].from_name;

                if (text.length() == 0) continue;

                if (isMessageProcessed(messageId)) {
                    Serial.printf("🔄 Пропускаем дубль ID=%ld\n", messageId);
                    continue;
                }

                Serial.printf("✅ Обработка ID=%ld: %s\n", messageId, text.c_str());
                processTelegramCommand(chat_id, text, from_name);
            }

            // Обновляем offset
            long lastProcessedId = bot->last_message_received;
            for (int i = 0; i < toProcess; i++) {
                if (bot->messages[i].message_id > lastProcessedId) {
                    lastProcessedId = bot->messages[i].message_id;
                }
            }
            bot->last_message_received = lastProcessedId;
        }
    } catch (...) {
        Serial.println("❌ Ошибка при работе с Telegram");
    }
}

void processTelegramCommand(String chat_id, String text, String from_name) {
    UserLevel userLevel = getUserLevel(chat_id);
    String cmd = text;
    cmd.toLowerCase();

    if (text.startsWith("/register")) {
        handleRegistration(chat_id, text, from_name);
        return;
    }

    if (userLevel == LEVEL_NONE) {
        sendTelegramMessage(chat_id,
            "🔒 *Доступ запрещён!*\nДля доступа отправьте:\n/register " REGISTER_PASSWORD,
            "Markdown");
        return;
    }

    if (cmd == "/start" || cmd == "/menu") {
        sendMainMenu(chat_id, from_name, userLevel);
    } else if (cmd == "/help" || cmd == "/commands") {
        sendAllCommands(chat_id);
    } else if (cmd == "/info" || cmd == "/about") {
        String info = "🔥 *Умный котёл v4.1*\n";
        String ip = WiFi.localIP().toString();
        info += "📡 WiFi: " + ip + "\n";
        info += "👥 Пользователей: " + String(userCount) + "\n";
        info += "⏱ Время работы: " + getUptimeString() + "\n";
        info += "🌐 Веб-интерфейс: http://" + ip;
        sendTelegramMessage(chat_id, info, "Markdown");
    } else if (cmd == "/status") {
        sendStatusToUser(chat_id);
    } else if (cmd == "/temp") {
        sendTemperaturesToUser(chat_id);
    } else if (cmd == "/water") {
        String msg = "💧 *Уровень воды:* ";
        msg += currentWaterOk ? "✅ НОРМА" : "❌ НИЗКИЙ";
        msg += "\nТемпература обратки: " + String(currentReturnTemp, 1) + "°C";
        sendTelegramMessage(chat_id, msg, "Markdown");
    } else if (userLevel == LEVEL_OWNER) {
        if (cmd == "/users") {
            String userList = "👥 *Список пользователей*\n";
            for (int j = 0; j < userCount; j++) {
                userList += String(j + 1) + ". " + users[j].name + "\n";
                userList += "   Уровень: " + getLevelString(users[j].level) + "\n";
                userList += "   ID: `" + users[j].chat_id + "`\n";
            }
            userList += "Всего: " + String(userCount) + " пользователей";
            sendTelegramMessage(chat_id, userList, "Markdown");
        } else if (cmd == "/adduser") {
            sendTelegramMessage(chat_id,
                "Для добавления пользователя отправьте:\n`/adduser [уровень] [chat_id]`\nУровни:\n`1` - Наблюдатель\n`2` - Пользователь\n`3` - Владелец\nПример: `/adduser 2 873535041`",
                "Markdown");
        } else if (cmd.startsWith("/adduser ")) {
            String params = cmd.substring(9);
            int spacePos = params.indexOf(' ');
            if (spacePos > 0) {
                int level = params.substring(0, spacePos).toInt();
                String new_chat_id = params.substring(spacePos + 1);
                new_chat_id.trim();
                if (level >= 1 && level <= 3 && new_chat_id.length() > 5) {
                    addUser(new_chat_id, (UserLevel)level, "Добавлен владельцем");
                    sendTelegramMessage(chat_id,
                        "✅ *Пользователь добавлен!*\nID: `" + new_chat_id + "`\nУровень: " + getLevelString((UserLevel)level),
                        "Markdown");
                }
            }
        } else if (cmd == "/removeuser") {
            sendTelegramMessage(chat_id,
                "Для удаления пользователя отправьте:\n`/removeuser [chat_id]`\nСписок пользователей: `/users`",
                "Markdown");
        } else if (cmd.startsWith("/removeuser ")) {
            String chat_id_to_remove = cmd.substring(12);
            chat_id_to_remove.trim();
            if (chat_id_to_remove == chat_id) {
                sendTelegramMessage(chat_id, "❌ *Нельзя удалить самого себя!*", "Markdown");
            } else {
                removeUser(chat_id_to_remove);
                sendTelegramMessage(chat_id, "✅ *Пользователь удалён:* `" + chat_id_to_remove + "`", "Markdown");
            }
        } else if (cmd == "/reset") {
            resetSafetyBlock();
            sendTelegramMessage(chat_id, "✅ *Ошибки сброшены*", "Markdown");
            notifyAll("Ошибки сброшены владельцем");
        } else if (cmd == "/restart") {
            sendTelegramMessage(chat_id, "🔄 *Перезагрузка системы...*", "Markdown");
            delay(1000);
            ESP.restart();
        } else {
            handleUserCommands(chat_id, cmd, from_name, userLevel);
        }
    } else if (userLevel >= LEVEL_USER) {
        handleUserCommands(chat_id, cmd, from_name, userLevel);
    } else if (userLevel == LEVEL_OBSERVER) {
        sendTelegramMessage(chat_id,
            "👁 *Вы наблюдатель*\nДоступно только:\n/status /temp /water /info",
            "Markdown");
    } else {
        sendTelegramMessage(chat_id, "❓ *Неизвестная команда*\nИспользуйте /help", "Markdown");
    }
}

void handleRegistration(String chat_id, String text, String from_name) {
    UserLevel userLevel = getUserLevel(chat_id);
    if (userLevel != LEVEL_NONE) {
        sendTelegramMessage(chat_id, "ℹ️ Вы уже зарегистрированы!", "Markdown");
        return;
    }

    String password = text.substring(9);
    password.trim();

    if (password == REGISTER_PASSWORD) {
        UserLevel newLevel = (userCount == 0) ? LEVEL_OWNER : LEVEL_OBSERVER;
        addUser(chat_id, newLevel, from_name);

        String welcome = "✅ *Регистрация успешна! v4.1*\n";
        welcome += "Привет, " + from_name + "!\n";
        welcome += "Уровень доступа: " + getLevelString(newLevel) + "\n";
        if (newLevel == LEVEL_OWNER) {
            welcome += "👑 Вы первый пользователь - ВЛАДЕЛЕЦ\n";
            welcome += "Для добавления других пользователей:\n";
            welcome += "`/adduser [уровень] [chat_id]`\n";
        }
        welcome += "*Новые возможности:*\n";
        welcome += "⚡ Миллисекундное управление\n";
        welcome += "🎯 Температура от 10°C\n";
        welcome += "🔒 Авто режим: макс " + String(MAX_POWER_AUTO) + "%\n";
        welcome += "Используйте /help для списка команд";
        sendTelegramMessage(chat_id, welcome, "Markdown");
    } else {
        sendTelegramMessage(chat_id,
            "❌ *Неверный пароль!*\nПравильно: /register " REGISTER_PASSWORD,
            "Markdown");
    }
}

void handleUserCommands(String chat_id, String cmd, String from_name, UserLevel level) {
    if (cmd == "/on") {
        currentMode = MODE_MAINTAIN;
        powerTarget = 0;
        saveSettings();
        sendTelegramMessage(chat_id,
            "✅ *Авто режим включён*\nЦель: " + String(targetReturnTemp) + "°C\nОграничение: макс " + String(MAX_POWER_AUTO) + "%",
            "Markdown");
        notifyAll(from_name + " включил авто режим");
    } else if (cmd == "/off") {
        currentMode = MODE_OFF;
        setPower(0);
        setRelay(false);
        saveSettings();
        sendTelegramMessage(chat_id, "⏹ *Котёл выключен*", "Markdown");
        notifyAll(from_name + " выключил котёл");
    } else if (cmd == "/manual") {
        currentMode = MODE_MANUAL;
        saveSettings();
        sendTelegramMessage(chat_id, "🔄 *Ручной режим включён*", "Markdown");
        notifyAll(from_name + " включил ручной режим");
    } else if (cmd.startsWith("/temp")) {
        if (cmd.length() > 5) {
            String numStr = cmd.substring(5);
            int temp = numStr.toInt();
            if (temp >= MIN_TARGET_TEMP && temp <= MAX_TARGET_TEMP) {
                targetReturnTemp = temp;
                saveSettings();
                String modeDesc = (temp <= 15) ? "❄️ Режим \"Не заморозить\"" :
                                  (temp <= 25) ? "💎 Экономный режим" : "🔥 Нормальный режим";
                sendTelegramMessage(chat_id,
                    "🎯 *Цель установлена:* " + String(temp) + "°C\n" + modeDesc,
                    "Markdown");
                notifyAll(from_name + " установил цель: " + String(temp) + "°C");
            } else {
                sendTelegramMessage(chat_id,
                    "⚠️ *Некорректная температура!*\nДопустимо: от " + String(MIN_TARGET_TEMP) + " до " + String(MAX_TARGET_TEMP) + "°C",
                    "Markdown");
            }
        }
    } else if (cmd.startsWith("/max")) {
        if (cmd.length() > 4) {
            String numStr = cmd.substring(4);
            int temp = numStr.toInt();
            if (temp >= 60 && temp <= 90) {
                maxFlowTemp = temp;
                saveSettings();
                sendTelegramMessage(chat_id, "🚫 *Макс. температура:* " + String(temp) + "°C", "Markdown");
            }
        }
    } else if (cmd.startsWith("/power")) {
        if (cmd.length() > 6) {
            String numStr = cmd.substring(6);
            int power = numStr.toInt();
            if (power >= 0 && power <= 100) {
                if (currentMode != MODE_MANUAL) {
                    sendTelegramMessage(chat_id, "⚠️ *Сначала включите ручной режим!*\nОтправьте: /manual", "Markdown");
                } else {
                    setPower(power);
                    saveSettings();
                    sendTelegramMessage(chat_id, "⚡️ *Мощность установлена:* " + String(power) + "%", "Markdown");
                    notifyAll(from_name + " установил мощность: " + String(power) + "%");
                }
            }
        }
    } else {
        sendTelegramMessage(chat_id, "❓ *Неизвестная команда*\nИспользуйте /help", "Markdown");
    }
}
