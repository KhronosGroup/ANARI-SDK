// Copyright 2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// A message says in its header whether a descriptor travels with it. One
// whose header and descriptor disagree is refused, and a descriptor that
// should not have come is not kept.

#include "TestSupport.h"

#include "common/Wire.h"

#include <fcntl.h>
#include <unistd.h>

using namespace anari_guard;

// One message with a payload of four bytes, its header claiming 'claimed'
// descriptors, sent with or without a region's descriptor beside it.
static void sendMessage(int endpoint, uint16_t claimed, int descriptor)
{
  Buffer message;
  {
    Writer w(message, Op::UNMAP_ARRAY, claimed);
    w.pod(uint32_t(0x01020304));
  }
  CHECK(descriptor < 0
          ? platform::sendAll(endpoint, message.data(), message.size())
          : platform::sendWithDescriptor(
                endpoint, message.data(), message.size(), descriptor));
}

int main()
{
  int a = -1, b = -1;
  CHECK(platform::makeChannelPair(a, b));

  platform::Region region;
  CHECK(platform::Region::create(4096, region));
  std::memcpy(region.data(), "contents", 8);

  MessageHeader header;
  Buffer payload;
  int descriptor = -1;

  // None claimed, none sent.
  sendMessage(a, 0, -1);
  CHECK(recvMessage(b, header, payload, descriptor));
  CHECK(descriptor == -1);
  CHECK(Op(header.opcode) == Op::UNMAP_ARRAY && header.descriptors == 0);
  CHECK(Reader(payload).pod<uint32_t>() == 0x01020304);

  // One claimed, one sent: the receiver can map what the sender made.
  sendMessage(a, 1, region.descriptor());
  CHECK(recvMessage(b, header, payload, descriptor));
  CHECK(descriptor >= 0 && header.descriptors == 1);
  CHECK(Reader(payload).pod<uint32_t>() == 0x01020304);
  platform::Region mapped;
  CHECK(platform::Region::map(descriptor, false, mapped));
  CHECK(mapped.size() == 4096);
  CHECK(std::memcmp(mapped.data(), "contents", 8) == 0);
  platform::closeDescriptor(descriptor);

  // One claimed, none sent.
  descriptor = -1;
  sendMessage(a, 1, -1);
  CHECK(!recvMessage(b, header, payload, descriptor));
  CHECK(descriptor == -1);

  // The refused message's payload is still in the channel: a real receiver
  // gives up on the channel here. This test starts a fresh one.
  platform::closeEndpoint(a);
  platform::closeEndpoint(b);
  CHECK(platform::makeChannelPair(a, b));

  // None claimed, one sent: refused, and the stray descriptor is closed.
  const int lowestFree = dup(0);
  close(lowestFree);
  sendMessage(a, 0, region.descriptor());
  CHECK(!recvMessage(b, header, payload, descriptor));
  CHECK(descriptor == -1);
  CHECK(fcntl(lowestFree, F_GETFD) == -1);

  platform::closeEndpoint(a);
  platform::closeEndpoint(b);
  CHECK(platform::makeChannelPair(a, b));

  // Two claimed: no message carries more than one.
  sendMessage(a, 2, region.descriptor());
  CHECK(!recvMessage(b, header, payload, descriptor));
  CHECK(descriptor == -1);

  std::printf("passed\n");
  return 0;
}
