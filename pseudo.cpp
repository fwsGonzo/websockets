#include <os>
#include <net/inet4>
#include "tcp_smp.hpp"
static_assert(SMP_MAX_CORES > 1, "SMP must be enabled");

static void tcp_service(net::TCP& tcp)
{
  // echo server
  auto& echo = tcp.listen(7,
  echo.on_connect(
    [] (auto conn) {
      conn->on_read(1024,
      [conn] (auto buf, size_t len) {
        conn->write(buf, len);
      });
    }));
}

void Service::start()
{
  // IP stack
  auto& inet = net::Inet4::ifconfig<>(0);
  inet.network_config(
      {  10, 0,  0, 42 },  // IP
      { 255,255,255, 0 },  // Netmask
      {  10, 0,  0,  1 },  // Gateway
      {  10, 0,  0,  1 }); // DNS

  TCP_over_SMP::init(inet, tcp_service);
}
