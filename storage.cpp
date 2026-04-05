/**
 * storage.cpp — Хранение настроек и пользователей (Preferences)
 */

#include "storage.h"
#include "control.h"
#include "telegram.h"

// Preferences
static Preferences prefs;

// ==================== НАСТРОЙКИ КОТЛА ====================

void loadSettings() {
    prefs.begin("boiler", true);  // readonly
    currentMode = (Mode)prefs.getUChar("mode", MODE_MAINTAIN);
    targetReturnTemp = prefs.getInt("target_temp", 40);
    maxFlowTemp = prefs.getInt("max_flow_temp", 75);
    hysteresis = prefs.getInt("hysteresis", 3);
    powerTarget = prefs.getInt("power", 0);
    prefs.end();
}

void saveSettings() {
    prefs.begin("boiler", false);  // write
    prefs.putUChar("mode", (uint8_t)currentMode);
    prefs.putInt("target_temp", targetReturnTemp);
    prefs.putInt("max_flow_temp", maxFlowTemp);
    prefs.putInt("hysteresis", hysteresis);
    prefs.putInt("power", powerTarget);
    prefs.end();
}

// ==================== ПОЛЬЗОВАТЕЛИ ====================

void loadUsers() {
    prefs.begin("users", true);  // readonly
    userCount = prefs.getInt("userCount", 0);

    for (int i = 0; i < userCount && i < MAX_USERS; i++) {
        String prefix = "user" + String(i);
        users[i].chat_id = prefs.getString((prefix + "_chat").c_str(), "");
        users[i].level = (UserLevel)prefs.getUChar((prefix + "_level").c_str(), LEVEL_OBSERVER);
        users[i].name = prefs.getString((prefix + "_name").c_str(), "");
    }
    prefs.end();
}

void saveUsers() {
    prefs.begin("users", false);  // write
    prefs.putInt("userCount", userCount);

    for (int i = 0; i < userCount; i++) {
        String prefix = "user" + String(i);
        prefs.putString((prefix + "_chat").c_str(), users[i].chat_id);
        prefs.putUChar((prefix + "_level").c_str(), (uint8_t)users[i].level);
        prefs.putString((prefix + "_name").c_str(), users[i].name);
    }
    prefs.end();
}

void addUser(String chat_id, UserLevel level, String name) {
    // Проверяем, есть ли уже такой пользователь
    for (int i = 0; i < userCount; i++) {
        if (users[i].chat_id == chat_id) {
            users[i].level = level;
            users[i].name = name;
            saveUsers();
            return;
        }
    }

    // Добавляем нового
    if (userCount < MAX_USERS) {
        users[userCount].chat_id = chat_id;
        users[userCount].level = level;
        users[userCount].name = name;
        userCount++;
        saveUsers();
    }
}

void removeUser(String chat_id) {
    for (int i = 0; i < userCount; i++) {
        if (users[i].chat_id == chat_id) {
            for (int j = i; j < userCount - 1; j++) {
                users[j] = users[j + 1];
            }
            userCount--;
            saveUsers();
            break;
        }
    }
}
