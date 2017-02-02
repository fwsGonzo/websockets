#include <service>
#include <net/inet4>
#include <deque>
#include "websocket.hpp"

static std::deque<net::WebSocket_ptr> websockets;

template <typename... Args>
static net::WebSocket_ptr& new_client(Args&&... args)
{
  for (auto& client : websockets)
  if (client->is_alive() == false) {
    client = nullptr;
    //return client = 
    //    net::WebSocket_ptr(new net::WebSocket(std::forward<Args> (args)...));
  }
  
  websockets.emplace_back(new net::WebSocket(std::forward<Args> (args)...));
  return websockets.back();
}

void websocket_service(net::Inet<net::IP4>& inet, uint16_t port)
{
  // buffer used for testing
  static net::tcp::buffer_t BUFFER;
  static const int          BUFLEN = 1000;
  BUFFER = decltype(BUFFER)(new uint8_t[BUFLEN]);
  
  using namespace http;
  static auto server = 
      std::make_unique<Server>(inet.tcp(), nullptr, std::chrono::seconds(0));

  server->on_request(
  [] (Request_ptr req, Response_writer_ptr writer)
  {
    auto& socket = new_client(std::move(req), std::move(writer));
    // if we are still connected, the handshake was accepted
    if (socket->is_alive())
    {
      socket->on_read =
      [] (const char* data, size_t len) {
        (void) data;
        (void) len;
        //printf("WebSocket on_read: %.*s\n", len, data);
      };
      
      socket->write("THIS IS A TEST CAN YOU HEAR THIS?");
      for (int i = 0; i < 1000; i++)
          socket->write(BUFFER, BUFLEN, net::WebSocket::BINARY);
      
      socket->setsum();
      //socket->close();
    }
  });
  server->listen(port);
}

void Service::start()
{
  // add own serial out after service start
  OS::add_stdout_default_serial();

  auto& inet = net::Inet4::ifconfig<>(0);
  inet.network_config(
      {  10, 0,  0, 42 },  // IP
      { 255,255,255, 0 },  // Netmask
      {  10, 0,  0,  1 },  // Gateway
      {  10, 0,  0,  1 }); // DNS

  websocket_service(inet, 8000);
}
