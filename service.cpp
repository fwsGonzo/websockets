#include <os>
#include <net/inet4>
#include <net/ws/connector.hpp>
#include <memdisk>
#include <https>
#include "tcp_smp.hpp"
#include <deque>

// configuration
static const bool ENABLE_TLS   = false;
static const bool TCP_OVER_SMP = false;
static_assert(SMP_MAX_CORES > 1 || TCP_OVER_SMP == false, "SMP must be enabled");

//#define DISABLE_CRASH_CONTEXT 1
#include <crash>

struct alignas(SMP_ALIGN) HTTP_server
{
  http::Server*      server = nullptr;
  net::tcp::buffer_t buffer = nullptr;
  net::WS_server_connector* ws_serve = nullptr;
  // websocket clients
  std::deque<net::WebSocket_ptr> clients;
};
static SMP_ARRAY<HTTP_server> httpd;

static net::WebSocket_ptr& new_client(net::WebSocket_ptr socket)
{
  auto& sys = PER_CPU(httpd);
  for (auto& client : sys.clients)
  if (client->is_alive() == false) {
    return client = std::move(socket);
  }

  sys.clients.push_back(std::move(socket));
  return sys.clients.back();
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

void websocket_service(net::TCP& tcp, uint16_t port)
{
  if (ENABLE_TLS)
  {
    auto& filesys = fs::memdisk().fs();
    // load CA certificate
    auto ca_cert = filesys.stat("/test.der");
    // load CA private key
    auto ca_key  = filesys.stat("/test.key");
    // load server private key
    auto srv_key = filesys.stat("/server.key");

    PER_CPU(httpd).server = new http::Secure_server(
          "blabla", ca_key, ca_cert, srv_key, tcp);
  }
  else
  {
    PER_CPU(httpd).server = new http::Server(tcp);
  }

  // buffer used for testing
  PER_CPU(httpd).buffer = net::tcp::construct_buffer(1024);

  // Set up server connector
  PER_CPU(httpd).ws_serve = new net::WS_server_connector(
    [&tcp] (net::WebSocket_ptr ws)
    {
      assert(SMP::cpu_id() == tcp.get_cpuid());
      // sometimes we get failed WS connections
      if (ws == nullptr) return;
      SET_CRASH("WebSocket created: %s", ws->to_string().c_str());

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
            socket->write(PER_CPU(httpd).buffer, net::op_code::BINARY);

        //socket->close();
      }
    },
    accept_client);
  PER_CPU(httpd).server->on_request(*PER_CPU(httpd).ws_serve);
  PER_CPU(httpd).server->listen(port);
  /// server ///
}

static void tcp_service(net::TCP& tcp)
{
  // echo server
  auto& echo = tcp.listen(7, [] (auto) {});
  echo.on_connect(
    [] (auto conn) {
      conn->on_read(1024,
      [conn] (auto buf) {
        conn->write(buf);
      });
    });

  // start a websocket server on @port
  websocket_service(tcp, 8000);
}

extern void recursive_task();
extern void allocating_task();
extern void per_cpu_task();
extern void exceptions_task();
extern void tls_task();

void Service::start()
{
  // IP stack
  auto& inet = net::Inet4::ifconfig<>(0);
  inet.network_config(
      {  10, 0,  0, 42 },  // IP
      { 255,255,255, 0 },  // Netmask
      {  10, 0,  0,  1 },  // Gateway
      {  10, 0,  0,  1 }); // DNS
  // Read-only filesystem
  fs::memdisk().init_fs(
  [] (auto err, auto&) {
    assert(!err);
  });

  if (TCP_OVER_SMP == false)
  {
    // run websocket server locally
    websocket_service(inet.tcp(), 8000);
  } else {
    // run websocket servers on CPUs
    init_tcp_smp_system(inet, tcp_service);
  }
}

#include <profile>
void Service::ready()
{
  //asm ("movq $0, %rax");
  //asm ("idivq %rax");
  //asm ("movl $0, %eax");
  //asm ("idivl %eax");
  // SMP exceptions is the main culprit
  //exceptions_task();

  if (SMP::cpu_id() != 0)
  {
    //recursive_task();
    //allocating_task();
    //per_cpu_task();
    tls_task();
    //exceptions_task();
  }

  //auto stats = ScopedProfiler::get_statistics();
  //printf("%.*s\n", stats.size(), stats.c_str());
}

void ws_client_test(net::TCP& tcp)
{
  /// client ///
  static http::Client client(tcp);
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
    PER_CPU(httpd).clients.push_back(std::move(socket));
  });
  /// client ///
}
