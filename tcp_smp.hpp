#pragma once
#include <net/inet4>

using tcp_service_func = delegate<void(net::TCP&)>;
using ip4_stack        = net::Inet<net::IP4>;

void init_tcp_smp_system(ip4_stack&, tcp_service_func);
