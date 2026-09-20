#include "web_server.h"

// Grace for a real request whose data is still on the way
static constexpr unsigned long IDLE_CLIENT_GRACE_MS = 200;

GenWebServer server(80);

void GenWebServer::handleClient()
{
  if (_currentStatus == HC_WAIT_READ && !_currentClient.available() &&
      millis() - _statusChange > IDLE_CLIENT_GRACE_MS && _server.hasClient())
  {
    _currentClient.stop();
    _currentClient = WiFiClient();
    _currentStatus = HC_NONE;
  }
  WebServer::handleClient();
}
