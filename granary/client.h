/* Copyright 2014 Peter Goodman, all rights reserved. */

#ifndef GRANARY_CLIENT_H_
#define GRANARY_CLIENT_H_

#include "granary/base/base.h"

namespace granary {

// Forward declarations.
class Client;

// Definition of a Granary client.
class Client {
 public:
  Client(void);

  virtual ~Client(void);

  virtual void Init(void) = 0;

 GRANARY_PUBLIC:
  GRANARY_CONST GRANARY_POINTER(Client) * const next;
};

#define GRANARY_ON_CLIENT_INIT(...) \
  static void OnClientInit(void); \
  namespace { \
  class : public Client { \
    using Client::Client; \
    virtual void Init(void) override { \
      OnClientInit(); \
    } \
  } static GRANARY_CAT(client,__LINE__) GRANARY_EARLY_GLOBAL; \
  } \
  static void OnClientInit(void)

// Initializes the clients.
GRANARY_INTERNAL_DEFINITION void InitClients(void);

// Exits the clients.
GRANARY_INTERNAL_DEFINITION void ExitClients(void);

}  // namespace granary

#endif  // GRANARY_CLIENT_H_
