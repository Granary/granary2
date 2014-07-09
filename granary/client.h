/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CLIENT_H_
#define GRANARY_CLIENT_H_

#define GRANARY_CLIENT_INIT_FUNC_NAME granary_client_init

namespace granary {

// Initialize a client.
#define GRANARY_CLIENT_INIT(...) \
  extern "C" { \
  GRANARY_EXPORT void GRANARY_CLIENT_INIT_FUNC_NAME(void) { \
    GRANARY_USING_NAMESPACE granary; \
    __VA_ARGS__ \
  } \
  }

#ifdef GRANARY_INTERNAL
namespace internal {
enum {
  MAX_CLIENT_NAME_LEN = 32
};

struct Client {
  char name[MAX_CLIENT_NAME_LEN];
  void *handle;
};

}  // namespace internal

// Scan the `clients` command line option and load each client.
//
// Defined in either `granary/user/client.cc` or `granary/kernel/client.cc`.
void LoadClients(const char *client_names, const char *granary_path);

// Unloads all clients.
void UnloadClients(void);

// Unloads a specific client.
void UnloadClient(void *);

// Registers a client (given its name as `client_name`). Returns `true` if the
// client was registered, or `false` if the client has already been registered.
internal::Client *RegisterClient(const char *client_name);

// Returns true if any client with name `client_name` has been loaded.
bool ClientIsRegistered(const char *client_name);
#endif  // GRANARY_INTERNAL

}  // namespace granary

#endif  // GRANARY_CLIENT_H_
