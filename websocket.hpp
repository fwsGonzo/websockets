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

#pragma once
#ifndef NET_WEBSOCKET_HPP
#define NET_WEBSOCKET_HPP

#include <net/http/server.hpp>

namespace net {

struct WebSocket
{
  typedef delegate<void(uint16_t)>    close_func;
  typedef delegate<void(std::string)> error_func;
  typedef delegate<void(const char*, size_t)> read_func;

  enum mode_t {
    TEXT,
    BINARY
  };

  void write(const char* buffer, size_t len, mode_t = TEXT);
  void write(net::tcp::buffer_t, size_t len, mode_t = TEXT);

  void write(const std::string& text)
  {
    write(text.c_str(), text.size(), TEXT);
  }

  // close the websocket
  void close();

  bool is_alive() const noexcept {
    return this->conn != nullptr;
  }
  size_t get_id() const noexcept {
    return this->id;
  }

  // callbacks
  close_func on_close = nullptr;
  error_func on_error = nullptr;
  read_func  on_read  = nullptr;

  WebSocket(http::Request_ptr req, 
            http::Response_writer_ptr writer);
  ~WebSocket();

  // string description for status codes
  static const char* status_code(uint16_t code);

private:
  void read_data(net::tcp::buffer_t, size_t);
  bool write_opcode(uint8_t code, const char*, size_t);
  void failure(const std::string&);
  void closed();
  void reset();
  
  net::tcp::Connection_ptr conn = nullptr;
  size_t id;
};
typedef std::unique_ptr<WebSocket> WebSocket_ptr;

}

#endif
