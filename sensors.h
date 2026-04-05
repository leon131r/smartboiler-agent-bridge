/**
 * sensors.h — Датчики и проверка безопасности
 */

#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include "config.h"

// Текущие показания датчиков
extern float currentReturnTemp;
extern float currentFlowTemp;
extern float currentAirTemp;
extern bool currentWaterOk;

// Состояние безопасности
extern bool safetyBlock;
extern String safetyReason;
extern unsigned long safetyStartTime;
extern String lastError;

// Инициализация датчиков
void initSensors();

// Чтение температуры NTC
float readNTC(int pin);

// Опрос всех датчиков
void readAllSensors();

// Проверка безопасности (возвращает false при аварии)
bool checkSafety(float t_flow, bool waterOk);

// Отправка алерта всем пользователям
void sendAlertToAll(const String& alert);

#endif // SENSORS_H
