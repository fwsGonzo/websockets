#pragma once

#include <botan/credentials_manager.h>
#include <botan/rng.h>
#include <botan/tls_server.h>
#include <botan/tls_callbacks.h>
#include <net/tcp/connection.hpp>
#include <net/tls/credman.hpp>

struct tls_smp_system
{
  void load_credentials(
      const std::string& name,
      fs::Dirent& ca_key,
      fs::Dirent& ca_cert,
      fs::Dirent& server_key);

  std::unique_ptr<Botan::Credentials_Manager> credman;
};
