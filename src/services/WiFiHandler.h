#pragma once

void initWiFi();
void handleWiFiReconnect();
// Перепідключитись з поточними даними з settings через 0.5 с (після відповіді HTTP)
void wifiApplyCreds();
