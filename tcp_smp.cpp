#include "tcp_smp.hpp"
#include <net/inet4>
#define SMP_DEBUG 1
#include <smp>

//#define DISABLE_CRASH_CONTEXT 1
#include <crash>

typedef net::tcp::Connection::Tuple tuple_t;

struct alignas(SMP_ALIGN) TCP_SMP
{
  // initialize from given IP stack
  void up(net::Inet<net::IP4>*);
  // handle redirected TCP traffic
  static void redirector(net::tcp::Packet_ptr);
  // TCP outgoing -> CPU 0 -> IP4 transmit
  void transmit(net::Packet_ptr);

  inline auto& tcp() { return *tcp_; }
private:
  net::IP4* ip4_out = nullptr;
  std::unique_ptr<net::TCP> tcp_ = nullptr;
};
static SMP_ARRAY<TCP_SMP> smp_system;

void TCP_SMP::transmit(net::Packet_ptr packet)
{
  // transport to CPU 0 and run it there
  SMP::add_bsp_task(
    SMP::task_func::make_packed(
    [this, pkt = std::move(packet)] () mutable {
      debug("Transmitting packet with len %u to %p\n", pkt->size(), ip4_out);
      SET_CRASH("Transmitting packet %p with len %u", pkt->buf(), pkt->size());
      ip4_out->transmit(std::move(pkt));
    }));
}

void TCP_SMP::up(net::Inet<net::IP4>* inet)
{
  debug("Creating TCP stack for CPU %d\n", SMP::cpu_id());
  ip4_out = &inet->ip_obj();
  tcp_.reset(new net::TCP(*inet, true));
  tcp_->set_network_out({this, &TCP_SMP::transmit});
}

static inline void guide(net::tcp::Packet_ptr packet, int cpu)
{
  SET_CRASH("Moving incoming packet %p len = %u to cpu %d",
            packet->buf(), packet->size(), cpu);
  SMP::add_task(
  SMP::task_func::make_packed(
    [cpu, pkt = std::move(packet)] () mutable {
      assert(PER_CPU(smp_system).tcp().get_cpuid() == SMP::cpu_id());
      assert(PER_CPU(smp_system).tcp().get_cpuid() == cpu);
      SET_CRASH("BEFORE Calling TCP::receive, packet %p len = %u",
                pkt->buf(), pkt->size());
      PER_CPU(smp_system).tcp().receive(std::move(pkt));
      SET_CRASH("AFTER Calling TCP::receive, packet %p len = %u",
                pkt->buf(), pkt->size());
    }), cpu);
  SMP::signal(cpu);
}

void TCP_SMP::redirector(net::tcp::Packet_ptr packet)
{
  debug("<redirector> Packet received - Source: %s, Destination: %s\n",
        packet->source().to_string().c_str(), packet->destination().to_string().c_str());

  static std::map<tuple_t, int> routes;
  const tuple_t tuple { packet->destination(), packet->source() };

  auto it = routes.find(tuple);
  if (it != routes.end())
  {
    debug("<redirector> Sending %s to %d\n",
            packet->source().to_string().c_str(), it->second);
    guide(std::move(packet), it->second);
    return;
  }

  debug("<redirector> Assigning new route for: %s\n",
          packet->source().to_string().c_str());
  // round-robin select vcpu
  static int next_cpu = 1;
  int current_cpu = next_cpu++;
  if (next_cpu >= SMP::cpu_count()) next_cpu = 1;

  // assign new route
  routes[tuple] = current_cpu;
  guide(std::move(packet), current_cpu);
}

void init_tcp_smp_system(ip4_stack& inet, tcp_service_func func)
{
  // start all the TCPs
  for (int cpu = 1; cpu < SMP::cpu_count(); cpu++)
  {
    SMP::add_task(
    SMP::task_func::make_packed(
      [cpu, network = &inet, func] () {
        SET_CRASH("Creating TCP system");
        PER_CPU(smp_system).up(network);
        SET_CRASH("Calling TCP over SMP user delegate for service code");
        func(PER_CPU(smp_system).tcp());
      }), cpu);
    SMP::signal(cpu);
  }
  // redirect inets TCP traffic to our guide
  inet.tcp().redirect(TCP_SMP::redirector);
}
