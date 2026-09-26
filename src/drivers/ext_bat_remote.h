#pragma once
#include <Arduino.h>

// Напруга АКБ з віддаленого модуля в локальній мережі (режим remote_voltage).
// Фонова задача раз на 5 с робить GET http://<IP>/status і бере поле "voltage".
// Поки модуль вимкнений у налаштуваннях або немає WiFi, задача спить.
struct RemoteBatReading
{
  bool valid; // остання вдала відповідь молодша за 30 с
  float voltage;
  uint32_t lastSuccessMs;
  int lastHttpCode;
};

bool initExtBatRemote();
RemoteBatReading getRemoteBatReading();
