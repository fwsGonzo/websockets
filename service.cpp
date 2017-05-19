#include <os>
#include <net/inet4>
#include <deque>
#include <net/ws/connector.hpp>

static std::deque<net::WebSocket_ptr> websockets;

static net::WebSocket_ptr& new_client(net::WebSocket_ptr socket)
{
  for (auto& client : websockets)
  if (client->is_alive() == false) {
    return client = std::move(socket);
  }

  websockets.push_back(std::move(socket));
  return websockets.back();
}

bool accept_client(net::Socket remote, std::string origin)
{
  /*
  printf("Verifying origin: \"%s\"\n"
         "Verifying remote: \"%s\"\n",
         origin.c_str(), remote.to_string().c_str());
  */
  (void) origin;
  return remote.address() == net::ip4::Addr(10,0,0,1);
}

#include <memdisk>
#include <https>

void websocket_service(net::Inet<net::IP4>& inet, uint16_t port)
{
  fs::memdisk().init_fs(
  [&inet, port] (auto err, auto& filesys) {
    assert(!err);

    // buffer used for testing
    static net::tcp::buffer_t BUFFER;
    static const int          BUFLEN = 1000;
    BUFFER = decltype(BUFFER)(new uint8_t[BUFLEN]);

    // load CA certificate
    auto ca_cert = filesys.stat("/test.der");
    // load CA private key
    auto ca_key  = filesys.stat("/test.key");
    // load server private key
    auto srv_key = filesys.stat("/server.key");

    using namespace http;
    // Set up a TCP server on port 443
    //static http::Secure_server httpd(
    //    "blabla", ca_key, ca_cert, srv_key, inet.tcp());
    static Server httpd(inet.tcp());

    // Set up server connector
    static net::WS_server_connector ws_serve(
      [] (net::WebSocket_ptr ws)
      {
        auto& socket = new_client(std::move(ws));
        // if we are still connected, attempt was verified and the handshake was accepted
        if (socket->is_alive())
        {
          socket->on_read =
          [] (auto message) {
            printf("WebSocket on_read: %.*s\n", (int) message->size(), message->data());
          };

          //socket->write("THIS IS A TEST CAN YOU HEAR THIS?");
          for (int i = 0; i < 1000; i++)
              socket->write(BUFFER, BUFLEN, net::op_code::BINARY);

          //socket->close();
        }
      },
      accept_client);
    httpd.on_request(ws_serve);
    httpd.listen(port);
    /// server ///
  });


  /// client ///
  static http::Client client(inet.tcp());
  net::WebSocket::connect(client, "ws://10.0.0.1:8001/",
  [] (net::WebSocket_ptr socket)
  {
    if (!socket) {
      printf("WS Connection failed!\n");
      return;
    }
    socket->on_error =
    [] (std::string reason) {
      printf("Socket error: %s\n", reason.c_str());
    };

    socket->write("HOLAS\r\n");
    websockets.push_back(std::move(socket));
  });
  /// client ///
}

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
void Service::ready()
{
  //auto stats = ScopedProfiler::get_statistics();
  //printf("%.*s\n", stats.size(), stats.c_str());
}
