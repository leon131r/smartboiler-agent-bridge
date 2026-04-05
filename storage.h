/**
 * storage.h — Хранение настроек и пользователей (Preferences)
 */

#ifndef STORAGE_H
#define STORAGE_H

#include <Arduino.h>
#include <Preferences.h>
#include "config.h"

// Загрузка настроек котла
void loadSettings();

// Сохранение настроек котла
void saveSettings();

// Загрузка пользователей
void loadUsers();

// Сохранение пользователей
void saveUsers();

// Добавление пользователя
void addUser(String chat_id, UserLevel level, String name);

// Удаление пользователя
void removeUser(String chat_id);

#endif // STORAGE_H
