#include <service>
#include <net/inet4>

void websocket_service(net::Inet<net::IP4>& inet, uint16_t port)
{
  auto& server = inet.tcp().bind(port);
  server.on_connect(
  [] (auto conn)
  {
    conn->on_read(9000,
    [conn] (net::tcp::buffer_t buf, size_t n)
    {
      printf("%.*s", n, buf.get());
    });
  });
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
