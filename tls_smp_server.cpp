// This file is a part of the IncludeOS unikernel - www.includeos.org
//
// Copyright 2017 Oslo and Akershus University College of Applied Sciences
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

#include "tls_smp_server.hpp"
#include "tls_smp_system.hpp"
#include <smp>

namespace http
{
  void TLS_SMP_server::load_credentials(
    const std::string& server_name,
    fs::Dirent& ca_key,
    fs::Dirent& ca_cert,
    fs::Dirent& server_key)
  {
    for (int i = 0; i < (int) system.size(); i++)
    {
      SMP::add_task(
      SMP::task_func::make_packed(
      [this, server_name, ca_key, ca_cert, server_key] () mutable
      {
        PER_CPU(system).load_credentials(
            server_name, ca_key, ca_cert, server_key);
      }), i);
      SMP::signal(i);
    }
  }

  void TLS_SMP_server::bind(const uint16_t port)
  {
    tcp_.listen(port, {this, &TLS_SMP_server::on_connect});
    INFO("TLS SMP server", "Listening on port %u", port);
  }

  void TLS_SMP_server::on_connect(TCP_conn conn)
  {
    // round-robin select vcpu
    static int next_cpu = 1;
    int current_cpu = next_cpu++;
    if (next_cpu >= SMP::cpu_count()) next_cpu = 1;

    // create TCP stream
    auto* ptr = new net::tls::SMP_client(conn, current_cpu);

    // create TLS stream on selected vcpu
    SMP::add_task(
    [this, ptr] ()
    {
      auto& sys = PER_CPU(system);
      net::tls::SMP_client::State_ptr state;
      state.reset(new net::tls::SMP_TLS_State(
                  *ptr,
                  sys.get_rng(),
                  *sys.credman));
      ptr->assign_tls(std::move(state));
    }, current_cpu);
    SMP::signal(current_cpu);

    // delay-set callbacks NOTE: don't move!
    ptr->on_connect(
    [this, ptr] (net::Stream&)
    {
      // create and pass TLS socket
      // this part is run back on main vcpu
      assert(SMP::cpu_id() == 0);
      connect(std::unique_ptr<net::tls::SMP_client>(ptr));
    });

    // this is ok due to the created Server_connection inside
    // connect assigns a new on_close
    ptr->on_close([ptr] {
      // this part is run back on main vcpu
      assert(SMP::cpu_id() == 0);
      delete ptr;
    });
  }

}
