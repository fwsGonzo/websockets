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
#include <util/base64.hpp>
#include <util/sha1.hpp>
#include <cstdint>

#define OPCODE_CONTINUE    0
#define OPCODE_TEXT        1
#define OPCODE_BINARY      2
#define OPCODE_CLOSE       8
#define OPCODE_PING        9
#define OPCODE_PONG       10
#define HEADER_MAXLEN     16

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
  void set_final() noexcept {
    bits |= 0x80;
    assert(is_final() == true);
  }
  uint16_t payload() const noexcept {
    return (bits >> 8) & 0x7f;
  }
  void set_payload(const size_t len)
  {
    uint16_t pbits = len;
    if      (len > 65535) pbits = 127;
    else if (len > 125)   pbits = 126;
    bits &= 0x80ff;
    bits |= (pbits & 0x7f) << 8;
    
    if (is_ext())
        *(uint16_t*) vla = __builtin_bswap16(len);
    else if (is_ext2())
        *(uint64_t*) vla = __builtin_bswap64(len);
    assert(data_length() == len);
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
  void set_opcode(uint8_t code) {
    bits &= ~0xf;
    bits |= code & 0xf;
    assert(opcode() == code);
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
  // assign unique id
  static size_t id_counter = 0;
  this->id = id_counter++;

  // validate handshake
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

  // create handshake response
  auto& header = writer->header();
  header.set_field(http::header::Connection, "Upgrade");
  header.set_field(http::header::Upgrade,    "WebSocket");
  header.set_field("Sec-WebSocket-Accept", encode_hash(key.to_string()));
  writer->write_header(http::Switching_Protocols);
  
  // we assume we are connected here
  this->conn = writer->connection();
  this->conn->on_read(16384, {this, &WebSocket::read_data});
  this->conn->on_close({this, &WebSocket::closed});
}

void WebSocket::read_data(net::tcp::buffer_t buf, size_t len)
{
  // silently ignore data from reset connection
  if (this->conn == nullptr) return;
  /// parse header
  if (len < sizeof(ws_header)) {
    failure("read_data: Header was too short");
    return;
  }
  ws_header& hdr = *(ws_header*) buf.get();
  /*
  printf("Code: %hhu  (%s) (final=%d)\n", 
          hdr.opcode(), opcode_string(hdr.opcode()), hdr.is_final());
  printf("Mask: %d  len=%u\n", hdr.is_masked(), hdr.mask_length());
  printf("Payload: len=%u dataofs=%u\n", 
          hdr.data_length(), hdr.data_offset());
  */
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
            this->on_close(__builtin_bswap16(reason));
      }
      else {
        if (this->on_close) this->on_close(1000);
      }
      // close it down
      this->closed();
      return;
  case OPCODE_PING:
      write_opcode(OPCODE_PONG);
      break;
  case OPCODE_PONG:
      break;
  default:
      printf("Unknown opcode: %u\n", hdr.opcode());
      break;
  }
}

static int make_header(char* dest, size_t len, uint8_t code)
{
  new (dest) ws_header;
  auto& hdr = *(ws_header*) dest;
  hdr.bits = 0;
  hdr.set_final();
  hdr.set_payload(len);
  hdr.set_opcode(code);
  
  if      (hdr.is_ext())  return 4;
  else if (hdr.is_ext2()) return 10;
  return 2;
}

void WebSocket::write(const char* buffer, size_t len, mode_t mode)
{
  if (this->conn == nullptr) {
    failure("write: Already closed");
    return;
  }
  if (this->conn->is_writable() == false) {
    failure("write: Connection not writable");
    return;
  }
  uint8_t opcode = (mode == TEXT) ? OPCODE_TEXT : OPCODE_BINARY;
  /// write header
  char header[HEADER_MAXLEN];
  int  header_len = make_header(header, len, opcode);
  // silently ignore invalid headers
  if (header_len <= 0) return;
  this->conn->write(header, header_len);
  /// write buffer
  this->conn->write(buffer, len);
}
void WebSocket::write(net::tcp::buffer_t buffer, size_t len, mode_t mode)
{
  if (this->conn == nullptr) {
    failure("write: Already closed");
    return;
  }
  if (this->conn->is_writable() == false) {
    failure("write: Connection not writable");
    return;
  }
  uint8_t opcode = (mode == TEXT) ? OPCODE_TEXT : OPCODE_BINARY;
  /// write header
  char header[HEADER_MAXLEN];
  int  header_len = make_header(header, len, opcode);
  // silently ignore invalid headers
  if (header_len <= 0) return;
  this->conn->write(header, header_len);
  /// write shared buffer
  this->conn->write(buffer, len);
}
bool WebSocket::write_opcode(uint8_t code)
{
  if (conn == nullptr) return false;
  if (conn->is_writable() == false) return false;
  /// write header
  char header[HEADER_MAXLEN];
  int  header_len = make_header(header, 0, code);
  this->conn->write(header, header_len);
  return true;
}
void WebSocket::closed()
{
  this->reset();
}

WebSocket::~WebSocket()
{
  if (conn != nullptr && conn->is_writable())
      this->close();
}

void WebSocket::close()
{
  /// send CLOSE message
  this->write_opcode(OPCODE_CLOSE);
  /// close and unset socket
  this->conn->close();
  this->reset();
}

void WebSocket::reset()
{
  this->on_close = nullptr;
  this->on_error = nullptr;
  this->on_read  = nullptr;
  this->conn     = nullptr;
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
