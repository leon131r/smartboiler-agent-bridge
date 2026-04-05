/**
 * telegram.h — Telegram бот, очередь сообщений, пользователи
 */

#ifndef TELEGRAM_H
#define TELEGRAM_H

#include <Arduino.h>
#include <UniversalTelegramBot.h>
#include <WiFiClientSecure.h>
#include "config.h"

// Структура пользователя
struct User {
    String chat_id;
    UserLevel level;
    String name;
};

// Структура сообщения для очереди
struct TelegramMessage {
    String chat_id;
    String text;
    String parse_mode;
};

// Структура обработанных сообщений (защита от дублей)
struct ProcessedMessage {
    long messageId;
    unsigned long receivedAt;
};

// Глобальные объекты Telegram
extern WiFiClientSecure telegramClient;
extern UniversalTelegramBot* bot;

// Пользователи
extern User users[MAX_USERS];
extern int userCount;

// Очередь сообщений
extern TelegramMessage messageQueue[MESSAGE_QUEUE_SIZE];
extern int messageQueueHead;
extern int messageQueueTail;
extern int messageQueueCount;

// Кэш обработанных сообщений
extern ProcessedMessage processedCache[PROCESSED_CACHE_SIZE];
extern int processedCacheCount;

// Автономный режим
extern bool autonomousMode;
extern int currentWifiIndex;
extern const WifiConfig wifiNetworks[];
extern const int wifiNetworkCount;
extern String getCurrentWifiSsid();

// === ФУНКЦИИ ОЧЕРЕДИ ===
void initTelegram();
void sendTelegramMessage(String chat_id, String text, String parse_mode = "Markdown");
void processTelegramQueue();
void cleanupMessageQueue();
bool isMessageProcessed(long messageId);

// === ФУНКЦИИ ПОЛЬЗОВАТЕЛЕЙ ===
UserLevel getUserLevel(String chat_id);
String getUserName(String chat_id);
String getLevelString(UserLevel level);
void addUser(String chat_id, UserLevel level, String name);
void removeUser(String chat_id);

// === УВЕДОМЛЕНИЯ ===
void notifyAll(String message);
void sendAlertToAll(const String& alert);
void notifyOwnerOnStartup();

// === ОБРАБОТКА СООБЩЕНИЙ ===
void checkTelegramMessages();
void processTelegramCommand(String chat_id, String text, String from_name);
void handleRegistration(String chat_id, String text, String from_name);
void handleUserCommands(String chat_id, String cmd, String from_name, UserLevel level);

// === ОТПРАВКА ДАННЫХ ===
void sendStatusToUser(String chat_id);
void sendTemperaturesToUser(String chat_id);
void sendAllCommands(String chat_id);
void sendMainMenu(String chat_id, String user_name, UserLevel level);

// === ВРЕМЯ ===
String getTimeString();
String getUptimeString();

#endif // TELEGRAM_H
