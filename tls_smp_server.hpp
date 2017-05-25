// This file is a part of the IncludeOS unikernel - www.includeos.org
//
// Copyright 2016-2017 Oslo and Akershus University College of Applied Sciences
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
#ifndef NET_TLS_SMP_SERVER_HPP
#define NET_TLS_SMP_SERVER_HPP

#include <net/http/server.hpp>
#include <fs/dirent.hpp>
#include "tls_smp_client.hpp"
#include "tls_smp_system.hpp"

namespace http {

/**
 * @brief      A secure HTTPS server.
 */
class TLS_SMP_server : public http::Server
{
public:
  /**
   * @brief      Construct a HTTPS server with the necessary certificates and keys.
   *
   * @param[in]  name         The name
   * @param      ca_key       The ca key
   * @param      ca_cert      The ca cert
   * @param      server_key   The server key
   * @param      tcp          The tcp
   * @param[in]  server_args  A list of args for constructing the underlying HTTP server
   *
   * @tparam     Server_args  Construct arguments to HTTP Server
   */
  template <typename... Server_args>
  inline TLS_SMP_server(
      const std::string& name,
      fs::Dirent& ca_key,
      fs::Dirent& ca_cert,
      fs::Dirent& server_key,
      net::TCP&   tcp,
      Server_args&&... server_args);

  /**
   * @brief      Loads credentials.
   *
   * @param[in]  name        The name
   * @param      ca_key      The ca key
   * @param      ca_cert     The ca cert
   * @param      server_key  The server key
   */
  void load_credentials(
      const std::string& name,
      fs::Dirent& ca_key,
      fs::Dirent& ca_cert,
      fs::Dirent& server_key);

private:
  SMP_ARRAY<tls_smp_system> system;

  /**
   * @brief      Binds TCP to pass all new connections to this on_connect.
   *
   * @param[in]  port  The port
   */
  void bind(const uint16_t port) override;

  /**
   * @brief      Try to upgrade a newly established TCP connection to a TLS connection.
   *
   * @param[in]  conn  The TCP connection
   */
  void on_connect(TCP_conn conn) override;

}; // < class Secure_server

template <typename... Server_args>
inline TLS_SMP_server::TLS_SMP_server(
    const std::string& name,
    fs::Dirent& ca_key,
    fs::Dirent& ca_cert,
    fs::Dirent& server_key,
    net::TCP&   tcp,
    Server_args&&... server_args)
  : Server{tcp, std::forward<Server>(server_args)...}
{
  load_credentials(name, ca_key, ca_cert, server_key);
}

} // < namespace http

#endif
