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

namespace net
{
struct ws_header
{
  uint16_t   fin     : 1;
  uint16_t   rsv1    : 1;
  uint16_t   rsv2    : 1;
  uint16_t   rsv3    : 1;
  uint16_t   opcode  : 4;
  uint16_t   mask    : 1;
  uint16_t   payload : 7;
  
  bool is_ext() const noexcept {
    return payload == 126;
  }
  bool is_ext2() const noexcept {
    return payload == 127;
  }
  bool is_key_masked() const noexcept {
    return mask != 0;
  }
  
  bool is_fail() const noexcept {
    return rsv1 != 0 || rsv2 != 0 || rsv3 != 0;
  }
  
  size_t mask_length() const noexcept {
    return (is_key_masked()) ? 4 : 0;
  }
  
  size_t data_offset() const noexcept {
    size_t len = mask_length();
    if (is_ext2()) return len + 8;
    if (is_ext())  return len + 2;
    return len;
  }
  size_t data_length() const noexcept {
    if (is_ext2())  return *(uint64_t*) vla;
    if (is_ext())   return *(uint16_t*) vla;
    return payload;
  }
  const char* data() const noexcept {
    return &vla[data_offset()];
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
  
  ws_header& hdr = *(ws_header*) buf.get();
  printf("Got header: len=%u dataofs=%u\n", 
          hdr.data_length(), hdr.data_offset());
  
  /// .. call on_read
  //on_read(data);
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
  if (this->on_close) this->on_close();
}

void WebSocket::failure(const std::string& reason)
{
  printf("Failure: %s\n", reason.c_str());
  if (conn != nullptr) conn->close();
  if (this->on_error) on_error(reason);
}

}
