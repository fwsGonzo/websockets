#include <service>
#include <net/inet4>
#include "websocket.hpp"

extern "C" char** get_cpu_esp();
std::vector<net::WebSocket_ptr> websockets;

void websocket_service(net::Inet<net::IP4>& inet, uint16_t port)
{
  using namespace http;
  static auto
  server = std::make_unique<Server>(inet.tcp(), nullptr, std::chrono::seconds(0));

  server->on_request(
  [] (Request_ptr req, Response_writer_ptr writer)
  {
    websockets.emplace_back(
        new net::WebSocket(std::move(req), std::move(writer)));
    
    auto& socket = websockets.back();
    socket->on_close =
    [] (uint16_t code) {
      printf("WebSocket closed: %s\n", net::WebSocket::status_code(code));
    };
    socket->on_read =
    [] (const char* data, size_t len) {
      printf("on_read: %.*s\n", len, data);
    };
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
  
  SHA1 checksum;
  checksum.update("abc");
  assert(checksum.as_hex() == "a9993e364706816aba3e25717850c26c9cd0d89d");

  checksum.update("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq");
  assert(checksum.as_hex() == "84983e441c3bd26ebaae4aa1f95129e5e54670f1");

  for (int i = 0; i < 1000000/200; ++i)
  {
      checksum.update("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                      "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                     );
  }
  assert(checksum.as_hex() == "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
}
