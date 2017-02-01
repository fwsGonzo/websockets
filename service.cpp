#include <service>
#include <net/inet4>
#include <deque>
#include "websocket.hpp"

static std::deque<net::WebSocket_ptr> websockets;

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
    websockets.emplace_back(
        new net::WebSocket(std::move(req), std::move(writer)));
    
    auto& socket = websockets.back();
    // if we are still connected, the handshake was accepted
    if (socket->is_alive())
    {
      socket->write("THIS IS A TEST CAN YOU HEAR THIS?");
      socket->on_close =
      [id = socket->get_id()] (uint16_t code) {
        
        for (size_t i = 0; i < websockets.size(); i++) {
            if (websockets[i]->get_id() == id) {
              printf("WebSocket erased: %s\n", net::WebSocket::status_code(code));
              websockets.erase(websockets.begin() + i);
            }
        }
      };
      socket->on_read =
      [] (const char* data, size_t len) {
        printf("WebSocket on_read: %.*s\n", len, data);
      };
      
      //socket->close();
      for (int i = 0; i < 10000; i++)
          socket->write(BUFFER, BUFLEN, net::WebSocket::BINARY);
      
      //socket->close();
    }
  });
  server->listen(port);
}

void Service::start()
{
  // add own serial out after service start
  OS::add_stdout_default_serial();

  auto& inet = net::Inet4::ifconfig<>();
  inet.network_config(
      {  10, 0,  0, 42 },  // IP
      { 255,255,255, 0 },  // Netmask
      {  10, 0,  0,  1 },  // Gateway
      {  10, 0,  0,  1 }); // DNS

  websocket_service(inet, 8000);
}
