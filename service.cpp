#include <os>
#include <net/inet4>
#include <deque>
#include <net/http/websocket.hpp>
#include <net/http/ws_connector.hpp>

static std::deque<http::WebSocket_ptr> websockets;

static http::WebSocket_ptr& new_client(http::WebSocket_ptr socket)
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
#include <util/sha1.hpp>

static void websocket_service(net::Inet<net::IP4>& inet, uint16_t port)
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

    {
      printf("test.der:   %s\n", SHA1::oneshot_hex(ca_cert.read()).c_str());
      printf("test.key:   %s\n", SHA1::oneshot_hex(ca_key.read()).c_str());
      printf("server.key: %s\n", SHA1::oneshot_hex(srv_key.read()).c_str());
    }

    using namespace http;
    // Set up a TCP server on port 443
    //static http::Secure_server httpd(
    //    "blabla", ca_key, ca_cert, srv_key, inet.tcp());
    static Server httpd(inet.tcp());

    // Set up server connector
    static WS_server_connector ws_serve(
      [] (WebSocket_ptr ws)
      {
        auto& socket = new_client(std::move(ws));
        // if we are still connected, attempt was verified and the handshake was accepted
        if (socket->is_alive())
        {
          socket->on_read =
          [] (const char* data, size_t len) {
            (void) data;
            (void) len;
            printf("WebSocket on_read: %.*s\n", (int) len, data);
          };

          //socket->write("THIS IS A TEST CAN YOU HEAR THIS?");
          for (int i = 0; i < 1000; i++)
              socket->write(BUFFER, BUFLEN, http::WebSocket::BINARY);

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
  http::WebSocket::connect(client, "ws://10.0.0.1:8001/",
  [] (http::WebSocket_ptr socket)
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

static int counter = 0;
void Service::start()
{
  auto& inet = net::Inet4::ifconfig<>(0);
  inet.network_config(
      {  10, 0,  0, 42 },  // IP
      { 255,255,255, 0 },  // Netmask
      {  10, 0,  0,  1 },  // Gateway
      {  10, 0,  0,  1 }); // DNS

  websocket_service(inet, 8000);

  auto& echo = inet.tcp().listen(7);
  echo.on_connect(
    [] (auto conn) {
      conn->on_read(1024,
      [conn] (auto buf, size_t len) {
        conn->write(buf, len);
      });
    });
}
#include <profile>
void Service::ready()
{
  //auto stats = ScopedProfiler::get_statistics();
  //printf("%.*s\n", stats.size(), stats.c_str());
}
