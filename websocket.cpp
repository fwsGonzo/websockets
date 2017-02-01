// This file is a part of the IncludeOS unikernel - www.includeos.org
//
// Copyright 2016 Oslo and Akershus University College of Applied Sciences
// and Alfred Bratterud
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "websocket.hpp"
#include <cstdint>

#define OPCODE_CONTINUE    0
#define OPCODE_TEXT        1
#define OPCODE_BINARY      2
#define OPCODE_CLOSE       8
#define OPCODE_PING        9
#define OPCODE_PONG       10

const char* opcode_string(uint16_t code)
{
  switch (code) {
  case OPCODE_CONTINUE:
      return "Continuation frame";
  case OPCODE_TEXT:
      return "Text frame";
  case OPCODE_BINARY:
      return "Binary frame";
  case OPCODE_CLOSE:
      return "Connection close";
  case OPCODE_PING:
      return "Ping";
  case OPCODE_PONG:
      return "Pong";
  default:
      return "Reserved (unspecified)";
  }
}

namespace net
{
struct ws_header
{
  uint16_t bits;
  
  bool is_final() const noexcept {
    return (bits >> 7) & 1;
  }
  uint16_t payload() const noexcept {
    return (bits >> 8) & 0x7f;
  }
  bool is_ext() const noexcept {
    return payload() == 126;
  }
  bool is_ext2() const noexcept {
    return payload() == 127;
  }
  bool is_masked() const noexcept {
    return bits >> 15;
  }
  
  uint8_t opcode() const noexcept {
    return bits & 0xf;
  }
  
  bool is_fail() const noexcept {
    return false;
  }
  
  size_t mask_length() const noexcept {
    return is_masked() ? 4 : 0;
  }
  const char* keymask() const noexcept {
    return &vla[data_offset() - mask_length()];
  }
  
  size_t data_offset() const noexcept {
    size_t len = mask_length();
    if (is_ext2()) return len + 8;
    if (is_ext())  return len + 2;
    return len;
  }
  size_t data_length() const noexcept {
    if (is_ext2())
        return __builtin_bswap64(*(uint64_t*) vla) & 0xffffffff;
    if (is_ext())
        return __builtin_bswap16(*(uint16_t*) vla);
    return payload();
  }
  const char* data() const noexcept {
    return &vla[data_offset()];
  }
  char* data() noexcept {
    return &vla[data_offset()];
  }
  void demask_data()
  {
    char* ptr  = data();
    const char* mask = keymask();
    for (size_t i = 0; i < data_length(); i++)
    {
      ptr[i] = ptr[i] xor mask[i & 3];
    }
  }
  
  size_t reported_length() const noexcept {
    return sizeof(ws_header) + data_offset() + data_length();
  }
  
  char vla[0];
} __attribute__((packed));

static inline std::string
encode_hash(const std::string& key)
{
  static const std::string GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  std::string hash  = SHA1::oneshot_raw(key + GUID);
  return base64::encode(hash);
}

WebSocket::WebSocket(
    http::Request_ptr req, 
    http::Response_writer_ptr writer)
{
  printf("Request: %s\n", req->to_string().c_str());
  auto view = req->header().value("Sec-WebSocket-Version");
  if (view == nullptr || view != "13") {
    writer->write_header(http::Bad_Request);
    if (on_error) on_error("Invalid version field");
    return;
  }

  auto key = req->header().value("Sec-WebSocket-Key");
  if (key == nullptr || key.size() < 16) {
    writer->write_header(http::Bad_Request);
    if (on_error) on_error("Invalid key field (too short)");
    return;
  }

  auto& header = writer->header();
  header.set_field(http::header::Connection, "Upgrade");
  header.set_field(http::header::Upgrade,    "WebSocket");
  header.set_field("Sec-WebSocket-Accept", encode_hash(key.to_string()));
  writer->write_header(http::Switching_Protocols);
  
  // we assume we are connected here
  this->conn = writer->connection();
  this->conn->on_read(16384, {this, &WebSocket::read_data});
  this->conn->on_close({this, &WebSocket::closed});
  if (on_connect) on_connect();
}

void WebSocket::read_data(net::tcp::buffer_t buf, size_t len)
{
  if (this->conn == nullptr) {
    failure("read_data: Already closed");
    return;
  }
  /// parse header
  if (len < sizeof(ws_header)) {
    failure("read_data: Header was too short");
    return;
  }
  printf("WebSocket header: %u\n", sizeof(ws_header));
  
  ws_header& hdr = *(ws_header*) buf.get();
  printf("Code: %hhu  (%s) (final=%d)\n", 
          hdr.opcode(), opcode_string(hdr.opcode()), hdr.is_final());
  printf("Mask: %d  len=%u\n", hdr.is_masked(), hdr.mask_length());
  printf("Payload: len=%u dataofs=%u\n", 
          hdr.data_length(), hdr.data_offset());
  
  /// validate payload length
  if (hdr.reported_length() != len) {
    failure("read: Invalid length");
    return;
  }
  /// unmask data (if masked)
  if (hdr.is_masked())
      hdr.demask_data();
  
  switch (hdr.opcode()) {
  case OPCODE_TEXT:
  case OPCODE_BINARY:
      /// .. call on_read
      if (on_read) {
          on_read(hdr.data(), hdr.data_length());
      }
      break;
  case OPCODE_CLOSE:
    // they are angry with us :(
    if (hdr.data_length() >= 2) {
      // provide reason to user
      uint16_t reason = *(uint16_t*) hdr.data();
      if (this->on_close)
          printf("CLOSE %u\n", reason);
          this->on_close(__builtin_bswap16(reason));
    }
    else {
      if (this->on_close) this->on_close(1000);
    }
    // close it down
    this->closed();
    return;
  default:
      printf("Unknown opcode: %u\n", hdr.opcode());
      break;
  }
}

void WebSocket::write(const char* buffer, size_t len)
{
  if (this->conn == nullptr) {
    failure("write: Already closed");
    return;
  }
  if (this->conn->is_writable() == false) {
    failure("write: Connection not writable");
    return;
  }
  /// write header
  
  /// write buffer
  
}
void WebSocket::write(net::tcp::buffer_t buffer, size_t len)
{
  if (this->conn == nullptr) {
    failure("write: Already closed");
    return;
  }
  if (this->conn->is_writable() == false) {
    failure("write: Connection not writable");
    return;
  }
  /// write header
  
  /// write shared buffer
  
}
void WebSocket::closed()
{
  this->conn = nullptr;
}

void WebSocket::failure(const std::string& reason)
{
  printf("Failure: %s\n", reason.c_str());
  if (conn != nullptr) conn->close();
  if (this->on_error) on_error(reason);
}

const char* WebSocket::status_code(uint16_t code)
{
  switch (code) {
  case 1000:
      return "Closed";
  case 1001:
      return "Going away";
  case 1002:
      return "Protocol error";
  case 1003:
      return "Cannot accept data";
  case 1004:
      return "Reserved";
  case 1005:
      return "Status code not present";
  case 1006:
      return "Connection closed abnormally";
  case 1007:
      return "Non UTF-8 data received";
  case 1008:
      return "Message violated policy";
  case 1009:
      return "Message too big";
  case 1010:
      return "Missing extension";
  case 1011:
      return "Internal server error";
  case 1015:
      return "TLS handshake failure";
  default:
      return "Unknown status code";
  }
}

}
