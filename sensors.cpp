/**
 * sensors.cpp — Датчики и проверка безопасности
 */

#include "sensors.h"
#include "telegram.h"

// Текущие показания датчиков
float currentReturnTemp = 0;
float currentFlowTemp = 0;
float currentAirTemp = 0;
bool currentWaterOk = true;

// Состояние безопасности
bool safetyBlock = false;
String safetyReason = "";
unsigned long safetyStartTime = 0;
String lastError = "OK";

void initSensors() {
    pinMode(HERCON_PIN, INPUT_PULLUP);
}

float readNTC(int pin) {
    int raw1 = analogRead(pin);
    delay(1);
    int raw2 = analogRead(pin);
    float raw = (raw1 + raw2) / 2.0f;

    // Проверка на обрыв/КЗ
    if (raw <= 10) return -100.0f;
    if (raw >= 4080) return -200.0f;

    // Расчёт сопротивления термистора
    float R_ntc = 10000.0f * (4095.0f / raw - 1.0f);
    if (R_ntc < 100.0f || R_ntc > 1000000.0f) return -300.0f;

    // Первичная оценка температуры для выбора коэффициентов
    float steinhart_guess = log(R_ntc / R_NOMINAL_WARM) / B_COEFF_WARM;
    steinhart_guess += 1.0f / (25.0f + 273.15f);
    float temp_guess = 1.0f / steinhart_guess - 273.15f;

    // Выбор коэффициентов в зависимости от диапазона
    float R_nominal_used, B_coeff_used;
    if (temp_guess <= TEMP_LOW_BOUND) {
        R_nominal_used = R_NOMINAL_COLD;
        B_coeff_used = B_COEFF_COLD;
    } else if (temp_guess >= TEMP_HIGH_BOUND) {
        R_nominal_used = R_NOMINAL_WARM;
        B_coeff_used = B_COEFF_WARM;
    } else {
        float mix_factor = (temp_guess - TEMP_LOW_BOUND) / (TEMP_HIGH_BOUND - TEMP_LOW_BOUND);
        R_nominal_used = R_NOMINAL_COLD + (R_NOMINAL_WARM - R_NOMINAL_COLD) * mix_factor;
        B_coeff_used = B_COEFF_COLD + (B_COEFF_WARM - B_COEFF_COLD) * mix_factor;
    }

    // Финальный расчёт по уравнению Стейнхарта-Харта
    float steinhart_final = log(R_ntc / R_nominal_used) / B_coeff_used;
    steinhart_final += 1.0f / (25.0f + 273.15f);
    float temperature = 1.0f / steinhart_final - 273.15f;

    // Проверка на физическую корректность
    if (temperature < -50.0f || temperature > 150.0f) {
        return -400.0f;
    }

    return temperature;
}

void readAllSensors() {
    currentReturnTemp = readNTC(NTC_RETURN_PIN);
    currentFlowTemp = readNTC(NTC_FLOW_PIN);
    currentAirTemp = readNTC(NTC_AIR_PIN);
    currentWaterOk = digitalRead(HERCON_PIN);
}

bool checkSafety(float t_flow, bool waterOk) {
    static unsigned long lastFlowWarning = 0;

    // Критическая защита по датчику подачи
    if (t_flow >= CRITICAL_TEMP - 2.0f) {
        if (!safetyBlock) {
            sendAlertToAll("🔥 *КРИТИЧЕСКАЯ ЗАЩИТА!* Верхняя точка котла: " +
                          String(t_flow, 1) + "°C (макс: " + String(CRITICAL_TEMP) + "°C)");
        }
        safetyBlock = true;
        safetyReason = "OVERHEAT_CRITICAL_FLOW";
        lastError = safetyReason;
        return false;
    }

    // Предупреждение по датчику подачи
    if (t_flow >= WARNING_TEMP - 5.0f && millis() - lastFlowWarning > 60000UL) {
        sendAlertToAll("⚠️ *Внимание!* Температура подачи: " +
                      String(t_flow, 1) + "°C (макс настроена: " + String(WARNING_TEMP) + "°C)");
        lastFlowWarning = millis();
    }

    // Проверка уровня воды
    if (!waterOk) {
        if (!safetyBlock) {
            sendAlertToAll("❗️ *НИЗКИЙ УРОВЕНЬ ВОДЫ!* Датчик геркон не сработал");
        }
        safetyBlock = true;
        safetyReason = "WATER_LOW";
        safetyStartTime = millis();
        lastError = safetyReason;
        return false;
    }

    // Восстановление после блокировки
    if (safetyBlock) {
        if (waterOk && t_flow < (WARNING_TEMP - 10.0f)) {
            safetyBlock = false;
            safetyReason = "";
            lastError = "OK";
            sendAlertToAll("✅ *Безопасность восстановлена*\nВерхняя точка: " +
                          String(t_flow, 1) + "°C\nУровень воды: НОРМА");
        }
    }

    return !safetyBlock;
}
