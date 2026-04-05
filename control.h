/**
 * control.h — Управление реле, алгоритм Брезенхема, авто-мощность
 */

#ifndef CONTROL_H
#define CONTROL_H

#include <Arduino.h>
#include "config.h"

// Состояние реле
extern bool actualRelayState;
extern bool reportedRelayState;
extern unsigned long relayStateChangeTime;

// Алгоритм Брезенхема
extern bool bresenhamTick;
extern unsigned long lastBresenhamTime;
extern int bresenhamCounter;
extern int bresenhamPower;

// Режим и параметры
extern Mode currentMode;
extern int powerTarget;
extern int targetReturnTemp;
extern int maxFlowTemp;
extern int hysteresis;

// Инициализация управления
void initControl();

// Установка реле (с защитой от дребезга)
void setRelay(bool state);

// Установка мощности
void setPower(int power);

// Тик алгоритма Брезенхема (вызывать каждые BRESENHAM_PERIOD мс)
void updateBresenham();

// Расчёт автоматической мощности
int calculateAutoPower(float t_return, float t_flow);

// Строковое представление режима
String getModeString();

// Сброс блока безопасности (вызывается из telegram.cpp)
void resetSafetyBlock();

#endif // CONTROL_H
