/* Copyright 2014 Peter Goodman, all rights reserved. */

#define GRANARY_INTERNAL

#include "granary/base/list.h"

#include "granary/client.h"

namespace granary {
namespace {

typedef LinkedListIterator<Client> ClientIterator;

// Global list of Granary clients.
static Client *clients(nullptr);

}  // namespace

Client::Client(void)
    : next(clients) {
  clients = this;
}

Client::~Client(void) {}

// Initializes the clients.
void InitClients(void) {
  for (auto client : ClientIterator(clients)) {
    client->Init();
  }
}

}  // namespace granary
