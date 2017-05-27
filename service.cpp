#include <os>
#include <net/inet4>
#include <deque>
#include <net/ws/connector.hpp>
#include "tcp_smp.hpp"

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

struct alignas(SMP_ALIGN) HTTP_server
{
  http::Server*      server = nullptr;
  net::tcp::buffer_t buffer = nullptr;
  net::WS_server_connector* ws_serve = nullptr;
};
static SMP_ARRAY<HTTP_server> httpd;

void websocket_service(net::TCP& tcp, uint16_t port)
{
  // toggle me
  static const bool SECURE = false;
  if (SECURE)
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
  static const int BUFLEN = 1000;
  PER_CPU(httpd).buffer = net::tcp::buffer_t(new uint8_t[BUFLEN]);

  // Set up server connector
  PER_CPU(httpd).ws_serve = new net::WS_server_connector(
    [&tcp] (net::WebSocket_ptr ws)
    {
      assert(SMP::cpu_id() == tcp.get_cpuid());
      // sometimes we get failed WS connections
      if (ws == nullptr) return;

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
            socket->write(PER_CPU(httpd).buffer, BUFLEN, net::op_code::BINARY);

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
      [conn] (auto buf, size_t len) {
        conn->write(buf, len);
      });
    });

  // start a websocket server on @port
  websocket_service(tcp, 8000);
}

void Service::start()
{
  auto& inet = net::Inet4::ifconfig<>(0);
  inet.network_config(
      {  10, 0,  0, 42 },  // IP
      { 255,255,255, 0 },  // Netmask
      {  10, 0,  0,  1 },  // Gateway
      {  10, 0,  0,  1 }); // DNS

  fs::memdisk().init_fs(
  [] (auto err, auto&) {
    assert(!err);
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

  // run websocket server locally
  websocket_service(inet.tcp(), 8000);

  // run websocket servers on CPUs
  //init_tcp_smp_system(inet, tcp_service);
}

static void recursive_task();
static void allocating_task();
static void per_cpu_task();

#include <profile>
#include <smp>
void Service::ready()
{
  if (SMP::cpu_id() != 0)
  {
    //recursive_task();
    //allocating_task();
    //per_cpu_task();
  }

  //auto stats = ScopedProfiler::get_statistics();
  //printf("%.*s\n", stats.size(), stats.c_str());
}

struct alignas(SMP_ALIGN) taskdata_t
{
  int count = 0;
};
static SMP_ARRAY<taskdata_t> taskdata;

void recursive_task()
{
  SMP::global_lock();
  printf("Starting recurring tasks on %d\n", SMP::cpu_id());
  SMP::global_unlock();

  SMP::add_bsp_task(
    [x = SMP::cpu_id()] () {
      SMP::global_lock();
      printf("%d: Back on main CPU!\n", x);
      SMP::global_unlock();
      SMP::add_task(
        [x] () {
          SMP::global_lock();
          printf("%d: Back on my CPU (%d)!\n", x, SMP::cpu_id());
          SMP::global_unlock();
          // go back to main CPU
          recursive_task();
        }, x);
      SMP::signal(x);
    });
}

static const int ALLOC_LEN = 1024*1024*1;

void allocating_task()
{
  // alloc data with cpuid as member
  auto* y = new char[ALLOC_LEN];
  for (int i = 0; i < ALLOC_LEN; i++) y[i] = SMP::cpu_id();

  SMP::add_bsp_task(
    [x = SMP::cpu_id(), y] ()
    {
      // verify and delete data
      for (int i = 0; i < ALLOC_LEN; i++)
        assert(y[i] == x);
      delete[] y;
      // reallocate, do it again
      auto* y = new char[ALLOC_LEN];
      memset(y, x, ALLOC_LEN);

      SMP::add_task(
        [x, y] () {
          assert(x == SMP::cpu_id());
          // verify and deallocate data
          for (int i = 0; i < ALLOC_LEN; i++)
            assert(y[i] == x);
          delete[] y;
          // show the task finished successfully
          PER_CPU(taskdata).count++;
          SMP::global_lock();
          printf("%d: Finished task successfully %d times!\n",
                SMP::cpu_id(), PER_CPU(taskdata).count);
          SMP::global_unlock();
          // go back to main CPU
          allocating_task();
        }, x);
      SMP::signal(x);
    });
}

static spinlock_t testlock = 0;
void per_cpu_task()
{
  SMP::add_bsp_task(
    [x = SMP::cpu_id()] () {
      // verify and delete data
      lock(testlock);
      assert(&PER_CPU(taskdata) == &taskdata[0]);
      unlock(testlock);

      SMP::add_task(
        [] {
          lock(testlock);
          assert(&PER_CPU(taskdata) == &taskdata[SMP::cpu_id()]);
          unlock(testlock);
          // show the task finished successfully
          PER_CPU(taskdata).count++;
          SMP::global_lock();
          printf("%d: Finished task successfully %d times!\n",
                SMP::cpu_id(), PER_CPU(taskdata).count);
          SMP::global_unlock();
          // go back to main CPU
          per_cpu_task();
        }, x);
      SMP::signal(x);
    });
}
