#include <os>
#include <net/inet4>
#include <deque>
#include "websocket.hpp"

//#define USE_SMP

static std::deque<net::WebSocket_ptr> websockets;

template <typename... Args>
static net::WebSocket_ptr& new_client(Args&&... args)
{
  for (auto& client : websockets)
  if (client->is_alive() == false) {
    return client = 
        net::WebSocket_ptr(new net::WebSocket(std::forward<Args> (args)...));
  }
  
  websockets.emplace_back(new net::WebSocket(std::forward<Args> (args)...));
  return websockets.back();
}

bool accept_client(net::tcp::Socket remote, std::string origin)
{
  /*
  printf("Verifying origin: \"%s\"\n"
         "Verifying remote: \"%s\"\n", 
         origin.c_str(), remote.to_string().c_str());
  */
  (void) origin;
  return remote.address() == net::ip4::Addr(10,0,0,1);
}

void websocket_service(net::Inet<net::IP4>& inet, uint16_t port)
{
  // buffer used for testing
  static net::tcp::buffer_t BUFFER;
  static const int          BUFLEN = 1000;
  BUFFER = decltype(BUFFER)(new uint8_t[BUFLEN]);
  
  /// server ///
  using namespace http;
  static auto server = 
      std::make_unique<Server>(inet.tcp(), nullptr, std::chrono::seconds(0));

  server->on_request(
  [] (Request_ptr req, Response_writer_ptr writer)
  {
    // create client websocket with accept function
    auto& socket = new_client(std::move(req), std::move(writer), accept_client);
    
    // if we are still connected, attempt was verified and the handshake was accepted
    if (socket->is_alive())
    {
      socket->on_read =
      [] (const char* data, size_t len) {
        (void) data;
        (void) len;
        printf("WebSocket on_read: %.*s\n", len, data);
      };
      
      //socket->write("THIS IS A TEST CAN YOU HEAR THIS?");
      for (int i = 0; i < 1000; i++)
          socket->write(BUFFER, BUFLEN, net::WebSocket::BINARY);
      
      //socket->close();
    }
  });
  server->listen(port);
  /// server ///

return;
  /// client ///
  static http::Client client(inet.tcp());
  net::WebSocket::connect(client, "ws://10.0.0.42", "ws://10.0.0.1:8001/", 
  [] (net::WebSocket_ptr socket)
  {
    if (!socket) {
      printf("WS Connection failed!\n");
      return;
    }
    printf("Connected!\n");
    websockets.push_back(std::move(socket));
    websockets.back()->write("HOLAS");
  });
  /// client ///
}

#ifdef USE_SMP
#include <smp>
void Service::start()
{
  SMP::add_task(
  [] {
    auto& inet = net::Inet4::ifconfig<>(0);
    inet.network_config(
        {  10, 0,  0, 42 },  // IP
        { 255,255,255, 0 },  // Netmask
        {  10, 0,  0,  1 },  // Gateway
        {  10, 0,  0,  1 }); // DNS

    websocket_service(inet, 8000);
  });
  SMP::signal();
}
#else
void Service::start()
{
  auto& inet = net::Inet4::ifconfig<>(0);
  inet.network_config(
      {  10, 0,  0, 42 },  // IP
      { 255,255,255, 0 },  // Netmask
      {  10, 0,  0,  1 },  // Gateway
      {  10, 0,  0,  1 }); // DNS

  websocket_service(inet, 8000);
}
#include <profile>
#include <timers>
void Service::ready()
{
  auto stats = ScopedProfiler::get_statistics();
  printf("%.*s\n", stats.size(), stats.c_str());
}
#endif
