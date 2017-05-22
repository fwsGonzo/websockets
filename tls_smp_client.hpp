// This file is a part of the IncludeOS unikernel - www.includeos.org
//
// Copyright 2015-2017 Oslo and Akershus University College of Applied Sciences
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
#ifndef NET_TLS_SMP_STREAM_HPP
#define NET_TLS_SMP_STREAM_HPP

#include <botan/credentials_manager.h>
#include <botan/rng.h>
#include <botan/tls_server.h>
#include <botan/tls_callbacks.h>
#include <net/tcp/connection.hpp>
#include <net/tls/credman.hpp>
#include "tls_smp_system.hpp"

//#define TLS_DEBUG 1

#ifdef TLS_DEBUG
#define TLS_PRINT(fmt, ...) \
    SMP::global_lock();     \
    printf(fmt, ##__VA_ARGS__); \
    SMP::global_unlock();
#else
#define TLS_PRINT(fmt, ...) /** fmt **/
#endif

namespace net
{
namespace tls
{
class SMP_client : public Botan::TLS::Callbacks, public tcp::Stream
{
public:
  using Connection_ptr = tcp::Connection_ptr;

  SMP_client(Connection_ptr remote,
             Botan::RandomNumberGenerator& rng,
             Botan::Credentials_Manager& credman)
  : tcp::Stream{remote},
    m_creds(credman),
    m_session_manager(),
    m_tls(*this, m_session_manager, m_creds, m_policy, rng),
    system_cpu(SMP::cpu_id())
  {
    assert(tcp->is_connected());
    // default read callback
    tcp->on_read(4096, {this, &SMP_client::bsp_read});
  }

  void on_read(size_t bs, ReadCallback cb) override
  {
    tcp->on_read(bs, {this, &SMP_client::bsp_read});
    this->o_read = cb;
  }
  void on_write(WriteCallback cb) override
  {
    this->o_write = cb;
  }
  void on_connect(ConnectCallback cb) override
  {
    this->o_connect = cb;
  }

  void write(const void* buffer, size_t n) override
  {
    // create buffer we have control over
    auto* data = new uint8_t[n];
    memcpy(data, buffer, n);
    buffer_t buf(data, std::default_delete<uint8_t> ());
    // ship it to vcpu
    write(std::move(buf), n);
  }
  void write(const std::string& str) override
  {
    this->write(str.data(), str.size());
  }
  void write(Chunk ch) override
  {
    SMP::add_task(
    [this, chunk = std::move(ch)] () {
        assert(this->active);
        m_tls.send(chunk.data(), chunk.size());
    }, this->system_cpu);
    SMP::signal(this->system_cpu);
  }
  void write(buffer_t buf, size_t n) override
  {
    TLS_PRINT("write(buffer_t) called on %d\n", SMP::cpu_id());
    SMP::add_task(
    [this, buff = std::move(buf), n] () {
      TLS_PRINT("write(): tls_send called on %d\n", SMP::cpu_id());
      assert(this->active);
      m_tls.send(buff.get(), n);
    }, this->system_cpu);
    SMP::signal(this->system_cpu);
  }

  std::string to_string() const override {
    return tcp->to_string();
  }

  void reset_callbacks() override
  {
    o_connect = nullptr;
    o_read  = nullptr;
    o_write = nullptr;
    tcp->reset_callbacks();
  }

protected:
  void bsp_read(buffer_t buf, const size_t n)
  {
    TLS_PRINT("bsp_read(): %lu bytes on %d\n", n, SMP::cpu_id());
    // execute tls_read on selected vcpu
    SMP::add_task(
    [this, buff = std::move(buf), n] () {
      try
      {
        TLS_PRINT("TLS process %lu bytes on %d\n", n, SMP::cpu_id());
        int rem = m_tls.received_data(buff.get(), n);
        (void) rem;
        //TLS_PRINT("Finished processing (rem: %u)\n", rem);
      }
      catch(Botan::Exception& e)
      {
        TLS_PRINT("Fatal TLS error %s\n", e.what());
        this->tls_close();
      }
      catch(...)
      {
        TLS_PRINT("Unknown error!\n");
        this->tls_close();
      }
    }, this->system_cpu);
    SMP::signal(this->system_cpu);
  }
  // close from TLS-side
  void tls_close()
  {
    this->active = false;
    TLS_PRINT("TLS close called on %d\n", SMP::cpu_id());
    SMP::add_bsp_task(
    [this] () {
      this->close();
    });
  }

  void tls_alert(Botan::TLS::Alert alert) override
  {
    // ignore close notifications
    if (alert.type() != Botan::TLS::Alert::CLOSE_NOTIFY)
    {
      TLS_PRINT("Got a %s alert: %s\n",
            (alert.is_fatal() ? "fatal" : "warning"),
            alert.type_string().c_str());
    }
  }

  bool tls_session_established(const Botan::TLS::Session&) override
  {
    // return true to store session
    return true;
  }

  void tls_emit_data(const uint8_t buf[], size_t len) override
  {
    TLS_PRINT("TLS emit %lu bytes on %d\n", len, SMP::cpu_id());
    auto buffff = buffer_t(new uint8_t[len]);
    memcpy(buffff.get(), buf, len);
    // run on main CPU
    SMP::add_bsp_task(
    [this, buff = std::move(buffff), len] () {
      TLS_PRINT("TCP write() %lu on %d\n", len, SMP::cpu_id());
      tcp->write(buff, len);
    });
  }

  void tls_record_received(uint64_t, const uint8_t buf[], size_t len) override
  {
    TLS_PRINT("TLS record %lu bytes on %d\n", len, SMP::cpu_id());
    if (o_read)
    {
      auto buffff = buffer_t(new uint8_t[len]);
      memcpy(buffff.get(), buf, len);
      // run on main CPU
      SMP::add_bsp_task(
      [this, buf = buffff, len] () {
        TLS_PRINT("BSP: Calling on_read on %d\n", SMP::cpu_id());
        o_read(buf, len);
      });
    }
  }

  void tls_session_activated() override
  {
    this->active = true;
    TLS_PRINT("TLS session connected on %d\n", SMP::cpu_id());
    if (o_connect) {
      SMP::add_bsp_task(
      [this] () {
        TLS_PRINT("Calling on_connect on %d\n", SMP::cpu_id());
        o_connect(*this);
      });
    }
  }

private:
  Stream::ReadCallback    o_read;
  Stream::WriteCallback   o_write;
  Stream::ConnectCallback o_connect;

  Botan::Credentials_Manager&   m_creds;
  Botan::TLS::Strict_Policy     m_policy;
  Botan::TLS::Session_Manager_Noop m_session_manager;

  Botan::TLS::Server m_tls;
  int  system_cpu = -1;
  bool active = false;
};

} // tls
} // net

#endif
