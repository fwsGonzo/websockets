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

namespace net
{
namespace tls
{
class SMP_client;

class SMP_TLS_State : public Botan::TLS::Callbacks
{
public:
  using Connection_ptr = tcp::Connection_ptr;
  using Buffer = std::unique_ptr<uint8_t[]>;

  SMP_TLS_State(
        SMP_client& in_stream,
        Botan::RandomNumberGenerator& rng,
        Botan::Credentials_Manager& credman)
  : stream(in_stream),
    m_creds(credman),
    m_session_manager(),
    m_tls(*this, m_session_manager, m_creds, m_policy, rng),
    system_cpu(SMP::cpu_id())
  {
    static int N = 0;
    stream_id = N++;
    TLS_PRINT("TLS stream %d constructed on %d\n",
              this->stream_id, SMP::cpu_id());
  }

  int get_id() const noexcept { return this->stream_id; }
  int get_cpuid() const noexcept { return this->system_cpu; }
  int is_active() const noexcept { return this->active; }

  void on_read(Stream::ReadCallback cb)
  {
    this->o_read = cb;
  }
  void on_connect(Stream::ConnectCallback cb)
  {
    this->o_connect = cb;
  }

  void reset()
  {
    o_connect = nullptr;
    o_read    = nullptr;
  }

  void read(tcp::buffer_t buff, size_t n);

  void write(tcp::buffer_t buff, size_t n);

  // close from TLS-side
  void close();

protected:
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

  void tls_emit_data(const uint8_t buf[], size_t len) override;

  void tls_record_received(uint64_t, const uint8_t buf[], size_t len) override;

  void tls_session_activated() override;

private:
  SMP_client& stream;
  Stream::ReadCallback    o_read    = nullptr;
  Stream::ConnectCallback o_connect = nullptr;

  Botan::Credentials_Manager&   m_creds;
  Botan::TLS::Strict_Policy     m_policy;
  Botan::TLS::Session_Manager_Noop m_session_manager;

  Botan::TLS::Server m_tls;
  int  system_cpu = -1;
  int  stream_id;
  bool active = false;
};

class SMP_client : public tcp::Stream
{
public:
  using Connection_ptr = tcp::Connection_ptr;
  using State_ptr = std::unique_ptr<SMP_TLS_State>;

  SMP_client(Connection_ptr remote, int cpu)
    : tcp::Stream{remote}, system_cpu(cpu)
  {
    assert(tcp->is_connected());
    // default read callback
    tcp->on_read(4096, {this, &SMP_client::bsp_read});
  }

  int get_id() const noexcept {
    if (tls_state) return tls_state->get_id();
    return -1;
  }

  void assign_tls(State_ptr state)
  {
    this->tls_state = std::move(state);
  }

  void on_read(size_t bs, ReadCallback cb) override
  {
    assert(SMP::cpu_id() == 0);
    tcp->on_read(bs, {this, &SMP_client::bsp_read});
    // probably safe:
    SMP::add_task(
    SMP::task_func::make_packed(
    [this, cb] () {
      assert(tls_state != nullptr);
      tls_state->on_read(cb);
    }), this->system_cpu);
    SMP::signal(this->system_cpu);
  }
  void on_write(WriteCallback cb) override
  {
    Stream::on_write(cb);
  }
  void on_connect(ConnectCallback cb) override
  {
    assert(SMP::cpu_id() == 0);
    SMP::add_task(
    SMP::task_func::make_packed(
    [this, cb] () {
      assert(tls_state != nullptr);
      this->tls_state->on_connect(cb);
    }), this->system_cpu);
    SMP::signal(this->system_cpu);
  }
  void on_close(CloseCallback cb) override
  {
    //TLS_PRINT("TLS %d on_close(): called on %d\n",
    //          get_id(), SMP::cpu_id());
    Stream::on_close(cb);
  }

  void write(const void* buffer, size_t n) override
  {
    // create buffer we have control over
    auto* data = new uint8_t[n];
    memcpy(data, buffer, n);
    buffer_t buf(data, std::default_delete<uint8_t[]> ());
    // ship it to vcpu
    write(std::move(buf), n);
  }
  void write(const std::string& str) override
  {
    this->write(str.data(), str.size());
  }
  void write(Chunk chunk) override
  {
    this->write(chunk.data(), chunk.size());
  }
  void write(buffer_t buf, size_t n) override
  {
    TLS_PRINT("TCP %d write(buffer_t) called on %d\n",
              get_id(), SMP::cpu_id());
    assert(tls_state != nullptr);
    assert(tls_state->is_active());

    SMP::add_task(
    [this, buff = std::move(buf), n] () {
      tls_state->write(std::move(buff), n);
    }, this->system_cpu);
    SMP::signal(this->system_cpu);
  }

  std::string to_string() const override {
    return tcp->to_string();
  }

  void reset_callbacks() override
  {
    tcp->reset_callbacks();
    if (tls_state) tls_state->reset();
  }

protected:
  void bsp_write(buffer_t buf, size_t n)
  {
    TLS_PRINT("TCP %d bsp_write(): %lu bytes on %d\n",
              get_id(), n, SMP::cpu_id());
    assert(SMP::cpu_id() == 0);
    Stream::write(std::move(buf), n);
  }
  void bsp_read(buffer_t buf, const size_t n)
  {
    TLS_PRINT("TCP %d bsp_read(): %lu bytes on %d\n",
              get_id(), n, SMP::cpu_id());
    assert(SMP::cpu_id() == 0);

    // execute tls_read on selected vcpu
    SMP::add_task(
    [this, buff = std::move(buf), n] () {
      assert(tls_state);
      this->tls_state->read(std::move(buff), n);
    }, this->system_cpu);
    SMP::signal(this->system_cpu);
  }

private:
  State_ptr tls_state = nullptr;
  int  system_cpu = -1;
  friend class SMP_TLS_State;
};

} // tls
} // net

#endif
