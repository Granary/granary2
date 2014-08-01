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

#define GRANARY_CLIENT_INIT(...) \
  namespace { \
  class : public Client { \
   public: \
    using Client::Client; \
    virtual void Init(void) override { \
     GRANARY_USING_NAMESPACE granary; \
      __VA_ARGS__ \
   } \
  } static client GRANARY_EARLY_GLOBAL; \
  }

// Initializes the clients.
GRANARY_INTERNAL_DEFINITION void InitClients(void);

}  // namespace granary

#endif  // GRANARY_CLIENT_H_
