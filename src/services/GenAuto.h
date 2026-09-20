#pragma once

// Режим Voltage: керування R1 по напрузі АКБ (A0).
// Пороги MIN/MAX зберігаються в EEPROM (initGenAuto() викликати після initEEPROM()).
// Працює лише в режимі GenMode::Voltage: замикає R1 при вході напруги в зону
// MIN і розмикає при вході в зону MAX. В інших режимах нічого не робить.
void initGenAuto();
void updateGenAuto();
// Забути, на яку зону вже відреагували — поточна напруга оцінюється заново
void resetGenAuto();

// false — пороги відхилено (не числа, поза 0..GEN_AUTO_BAT_LIMIT_V або min >= max)
bool setGenAutoThresholds(float minV, float maxV);
float getGenAutoMinV();
float getGenAutoMaxV();
