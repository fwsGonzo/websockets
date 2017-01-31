#include <service>
#include <net/inet4>
#include <net/http/server.hpp>
#include <util/base64.hpp>
extern "C" char** get_cpu_esp();

void websocket_service(net::Inet<net::IP4>& inet, uint16_t port)
{
  using namespace http;
  static auto
  server = std::make_unique<Server>(inet.tcp());

  server->on_request(
  [] (Request_ptr req, Response_writer writer)
  {
    printf("Receiving request:\n%s\n", req->to_string().c_str());

    auto& header = writer.header();
    header.set_field(header::Connection, "Upgrade");
    header.set_field(header::Upgrade,    "WebSocket");
    header.set_field("Sec-WebSocket-Key", base64::encode(*get_cpu_esp(), 16));

    auto& resp = writer.res();
    resp.set_status_code(Switching_Protocols);
    
    writer.send();
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
