#include "tls_smp_client.hpp"

using namespace net::tls;

void SMP_TLS_State::read(tcp::buffer_t buff)
{
  TLS_PRINT("TLS %d recv: process %lu bytes on CPU %d\n",
            this->stream_id, buff->size(), SMP::cpu_id());
  assert(SMP::cpu_id() == this->system_cpu);
  try
  {
    int rem = m_tls.received_data(buff->data(), buff->size());
    TLS_PRINT("TLS %d finished processing, %u rem\n",
              this->stream_id, rem);
    (void) rem;
  }
  catch(Botan::Exception& e)
  {
    TLS_ALWAYS_PRINT("TLS %d: TLS recv error %s\n",
              this->stream_id, e.what());
    this->close();
  }
  catch(std::exception& e)
  {
    TLS_ALWAYS_PRINT("TLS %d: TLS recv error %s!\n",
            this->stream_id, e.what());
    this->close();
  }
}

void SMP_TLS_State::write(tcp::buffer_t buff)
{
  TLS_PRINT("TLS %d write(): tls_send called on %d\n",
            this->get_id(), SMP::cpu_id());
  //assert(this->active);
  try
  {
    m_tls.send(buff->data(), buff->size());
  }
  catch(Botan::Exception& e)
  {
    TLS_ALWAYS_PRINT("TLS %d: TLS send error %s\n",
              this->stream_id, e.what());
    this->close();
  }
  catch(std::exception& e)
  {
    TLS_ALWAYS_PRINT("TLS %d: TLS send error %s!\n",
            this->stream_id, e.what());
    this->close();
  }
}

void SMP_TLS_State::close()
{
  assert(SMP::cpu_id() != 0);
  TLS_ALWAYS_PRINT("TLS %d close called on %d\n",
            this->stream_id, SMP::cpu_id());
  SMP::add_bsp_task(
  [this] () {
    stream.close();
  });
}


void SMP_TLS_State::tls_emit_data(const uint8_t buf[], size_t len)
{
  TLS_PRINT("TLS %d emit %lu bytes on %d\n",
            this->stream_id, len, SMP::cpu_id());
  assert(SMP::cpu_id() == this->system_cpu);

  auto buff = tcp::construct_buffer(buf, buf + len);
  // run on main CPU
  SMP::add_bsp_task(
  [this, buf = std::move(buff)] () {
    TLS_PRINT("TLS %d TCP write() %lu (writable=%d) on %d\n",
              this->stream_id, buf->size(), stream.is_writable(), SMP::cpu_id());
    stream.bsp_write(std::move(buf));
  });
}

void SMP_TLS_State::tls_record_received(
          uint64_t, const uint8_t buf[], size_t len)
{
  TLS_PRINT("TLS %d tls record %lu bytes on %d\n",
            this->stream_id, len, SMP::cpu_id());
  assert(SMP::cpu_id() == this->system_cpu);
  assert(this->active);

  if (o_read)
  {
    auto buff = tcp::construct_buffer(buf, buf + len);
    // run on main CPU
    SMP::add_bsp_task(
    [this, buf = std::move(buff)] () {
      if (o_read) {
        TLS_PRINT("TLS %d calling on_read on %d\n",
                  this->stream_id, SMP::cpu_id());
        o_read(std::move(buf));
      }
    });
  }
}

void SMP_TLS_State::tls_session_activated()
{
  TLS_PRINT("TLS %d session connected on %d\n",
            this->stream_id, SMP::cpu_id());
  assert(SMP::cpu_id() == this->system_cpu);
  this->active = true; // ACTIVATE!

  if (o_connect) {
    SMP::add_bsp_task(
    [this] () {
      if (o_connect) {
        TLS_PRINT("TLS %d calling on_connect on %d\n",
                  this->stream_id, SMP::cpu_id());
        o_connect(stream);
      }
    });
  }
}
