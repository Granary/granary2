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

static char registered_clients[MAX_NUM_REGISTERED_CLIENTS]
                              [MAX_CLIENT_NAME_LEN] = {{'\0'}};
}  // namespace

// Registers a client (given its name as `client_name`). Returns `true` if the
// client was registered, or `false` if the client has already been registered.
bool RegisterClient(const char *client_name) {
  if (!ClientIsRegistered(client_name)) {
    GRANARY_ASSERT(MAX_NUM_REGISTERED_CLIENTS > num_registered_clients);
    CopyString(registered_clients[num_registered_clients++],
               MAX_CLIENT_NAME_LEN, client_name);
    return true;
  }
  return false;
}

// Returns true if any client with name `client_name` has been loaded.
bool ClientIsRegistered(const char *client_name) {
  for (auto name : registered_clients) {
    if (*name) {
      if (StringsMatch(name, client_name)) {
        return true;
      }
    } else {
      return false;
    }
  }
  return false;
}

}  // namespace granary
