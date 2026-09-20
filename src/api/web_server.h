#pragma once
#include <WebServer.h>

// WebServer serves one connection at a time and waits up to 5 s for a request
// on an accepted socket. Browsers open spare connections that never send
// anything, which stalls every other request. This drops such an idle socket
// once another client is waiting.
class GenWebServer : public WebServer
{
public:
  using WebServer::WebServer;
  void handleClient() override;
};

extern GenWebServer server;
