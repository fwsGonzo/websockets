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
#ifndef TLS_SMP_SYSTEM_HPP
#define TLS_SMP_SYSTEM_HPP

#include <botan/credentials_manager.h>
#include <net/tls/credman.hpp>
#include <fs/dirent.hpp>
#include <smp>

//#define TLS_DEBUG 1

#define TLS_ALWAYS_PRINT(fmt, ...) \
    SMP::global_lock();     \
    printf(fmt, ##__VA_ARGS__); \
    SMP::global_unlock();

#ifdef TLS_DEBUG
#define TLS_PRINT(fmt, ...) TLS_ALWAYS_PRINT(fmt, ##__VA_ARGS__)
#else
#define TLS_PRINT(fmt, ...) /** fmt **/
#endif

struct alignas(SMP_ALIGN) tls_smp_system
{
  static Botan::RandomNumberGenerator& get_rng();

  void load_credentials(
      const std::string& name,
      fs::Dirent& ca_key,
      fs::Dirent& ca_cert,
      fs::Dirent& server_key);

  std::unique_ptr<Botan::Credentials_Manager> credman;
};

#endif
