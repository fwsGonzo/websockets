#include <service>
#include <net/inet4>
#include <net/http/server.hpp>
#include <util/base64.hpp>
#include <util/sha1.hpp>

extern "C" char** get_cpu_esp();

std::string encode_hash(const std::string& key)
{
  static const std::string GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  std::string hash  = SHA1::oneshot_raw(key + GUID);
  return base64::encode(hash);
}

void websocket_service(net::Inet<net::IP4>& inet, uint16_t port)
{
  using namespace http;
  static auto
  server = std::make_unique<Server>(inet.tcp());

  server->on_request(
  [] (Request_ptr req, Response_writer writer)
  {
    auto view = req->header().value("Sec-WebSocket-Version");
    if (view == nullptr || view != "13") {
      printf("Invalid version field\n");
      writer.res().set_status_code(Bad_Request);
      writer.send();
      return;
    }

    auto key = req->header().value("Sec-WebSocket-Key");
    if (key == nullptr || key.size() < 16) {
      printf("Invalid Key field: %s\n", key.data());
      writer.res().set_status_code(Bad_Request);
      writer.send();
      return;
    }

    auto& header = writer.header();
    header.set_field(header::Connection, "Upgrade");
    header.set_field(header::Upgrade,    "WebSocket");
    header.set_field("Sec-WebSocket-Accept", encode_hash(key.to_string()));

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

  // verify handshake
  assert(encode_hash("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}
