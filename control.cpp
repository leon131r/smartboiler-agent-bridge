/**
 * control.cpp — Управление реле, алгоритм Брезенхема, авто-мощность
 */

#include "control.h"
#include "sensors.h"

// Состояние реле
bool actualRelayState = false;
bool reportedRelayState = false;
unsigned long relayStateChangeTime = 0;

// Алгоритм Брезенхема
bool bresenhamTick = false;
unsigned long lastBresenhamTime = 0;
int bresenhamCounter = 0;
int bresenhamPower = 0;

// Режим и параметры
Mode currentMode = MODE_MAINTAIN;
int powerTarget = 0;
int targetReturnTemp = 40;
int maxFlowTemp = 75;
int hysteresis = 3;

// Безопасность (из sensors.cpp)
extern bool safetyBlock;
extern String safetyReason;
extern String lastError;
extern bool currentWaterOk;

// Автономный режим
extern bool autonomousMode;

void initControl() {
    pinMode(SSR_PIN, OUTPUT);
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(SSR_PIN, LOW);
    digitalWrite(LED_PIN, HIGH);
}

void setRelay(bool state) {
    unsigned long now = millis();
    if (now - relayStateChangeTime < RELAY_DEBOUNCE_TIME) return;

    actualRelayState = state;
    digitalWrite(SSR_PIN, state ? HIGH : LOW);
    relayStateChangeTime = now;

    if (reportedRelayState != actualRelayState) {
        reportedRelayState = actualRelayState;
    }
}

void setPower(int power) {
    int maxAllowed = (currentMode == MODE_MANUAL) ? MAX_POWER_MANUAL : MAX_POWER_AUTO;
    if (power > maxAllowed) power = maxAllowed;
    if (power < 0) power = 0;
    if (power > 100) power = 100;

    powerTarget = power;
}

void updateBresenham() {
    unsigned long now = millis();
    if (now - lastBresenhamTime < BRESENHAM_PERIOD) return;

    lastBresenhamTime = now;

    // При блокировке безопасности или выключенном режиме — реле выключено
    if (safetyBlock || currentMode == MODE_OFF) {
        bresenhamCounter = 0;
        bresenhamPower = 0;
        bresenhamTick = false;
        setRelay(false);
        return;
    }

    // Алгоритм Брезенхема для ШИМ
    bresenhamCounter += powerTarget;
    bresenhamTick = (bresenhamCounter >= 100);

    if (bresenhamTick) {
        bresenhamCounter -= 100;
        bresenhamPower = 100;
        setRelay(true);
    } else {
        bresenhamPower = 0;
        setRelay(false);
    }
}

int calculateAutoPower(float t_return, float t_flow) {
    // Режим "не заморозить" (температура ≤ 15°C)
    if (targetReturnTemp <= 15) {
        if (t_return <= 10.0f) {
            return MAX_POWER_AUTO;
        } else if (t_return <= targetReturnTemp - 0.5f) {
            return MAX_POWER_AUTO;
        } else if (t_return >= targetReturnTemp + 0.5f) {
            return 0;
        }
        return reportedRelayState ? MAX_POWER_AUTO : 0;
    }

    // Нормальный режим — защита по максимальной температуре потока
    if (t_flow >= maxFlowTemp - 2) return 0;

    // Управление по датчику обратки
    if (t_return <= targetReturnTemp - hysteresis) {
        int error = targetReturnTemp - (int)t_return;
        int power = map(error, 0, 20, 20, 100);
        if (power < 20) power = 20;
        if (currentMode == MODE_MAINTAIN && power > MAX_POWER_AUTO) power = MAX_POWER_AUTO;
        return power;
    } else if (t_return >= targetReturnTemp + 1) {
        return 0;
    }
    return 20; // Минимальная мощность для поддержания температуры
}

String getModeString() {
    switch (currentMode) {
        case MODE_OFF: return "Выключен";
        case MODE_MAINTAIN: return "Авто (" + String(MAX_POWER_AUTO) + "% макс)";
        case MODE_MANUAL: return "Ручной";
        default: return "Неизвестно";
    }
}

void resetSafetyBlock() {
    if (safetyBlock) {
        safetyBlock = false;
        safetyReason = "";
        lastError = "OK";
    }
}
