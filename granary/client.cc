/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/base.h"
#include "granary/base/string.h"

#include "granary/breakpoint.h"
#include "granary/client.h"

namespace granary {
namespace {
enum {
  MAX_NUM_REGISTERED_CLIENTS = 32
};

static int num_registered_clients = 0;

static internal::Client registered_clients[MAX_NUM_REGISTERED_CLIENTS] = {
  {{'\0'}, nullptr}
};

}  // namespace

// Unloads all clients.
void UnloadClients(void) {
  for (auto &client : registered_clients) {
    if (client.handle) {
      UnloadClient(client.handle);
      client.handle = nullptr;
    }
  }
}

// Registers a client (given its name as `client_name`). Returns `true` if the
// client was registered, or `false` if the client has already been registered.
internal::Client *RegisterClient(const char *client_name) {
  if (!ClientIsRegistered(client_name)) {
    GRANARY_ASSERT(MAX_NUM_REGISTERED_CLIENTS > num_registered_clients);
    auto &client(registered_clients[num_registered_clients++]);
    CopyString(client.name,
               internal::MAX_CLIENT_NAME_LEN, client_name);
    return &client;
  }
  return nullptr;
}

// Returns true if any client with name `client_name` has been loaded.
bool ClientIsRegistered(const char *client_name) {
  for (auto &client : registered_clients) {
    if (client.name[0]) {
      if (StringsMatch(client.name, client_name)) {
        return true;
      }
    } else {
      return false;
    }
  }
  return false;
}

}  // namespace granary
