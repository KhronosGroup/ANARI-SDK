// Copyright 2025-2026 The Khronos Group
// SPDX-License-Identifier: Apache-2.0

// ISPC objects hold raw pointers into IndexedVector slots, so a slot must keep
// its address for as long as it is occupied, however many others come and go.

#include "utility/IndexedVector.hpp"
#include "utility/IspcObjectStorage.hpp"
// std
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using halcyon::IndexedVector;
using Storage = halcyon::IspcObjectStorage<16>;

void *addr(const Storage &s)
{
  return s;
}

bool check(bool ok, const char *what)
{
  if (!ok)
    std::printf("FAIL %s\n", what);
  return ok;
}

// Enough insertions to grow any contiguous backing store several times over
constexpr size_t NUM_OBJECTS = 10000;

bool testAddressesSurviveGrowth()
{
  IndexedVector<Storage> iv;
  auto first = iv.emplace();
  void *firstAddress = addr(*first);
  first->data[0] = 42;

  std::vector<void *> addresses;
  for (size_t i = 0; i < NUM_OBJECTS; i++)
    addresses.push_back(addr(*iv.emplace()));

  bool pass = check(addr(iv[first.index()]) == firstAddress,
      "first slot moved when others were inserted");
  pass &= check(iv[first.index()].data[0] == 42, "first slot lost its data");
  for (size_t i = 0; i < NUM_OBJECTS && pass; i++) {
    pass &= check(
        addr(iv[i + 1]) == addresses[i], "a later slot moved on insertion");
  }
  std::printf("%s addresses survive growth\n", pass ? "PASS" : "FAIL");
  return pass;
}

bool testFreedSlotsAreReused()
{
  IndexedVector<Storage> iv;
  for (size_t i = 0; i < 8; i++)
    iv.emplace();
  void *kept = addr(iv[7]);
  iv.erase(3);
  iv.erase(5);

  bool pass = check(iv.size() == 6, "size after erase");
  const size_t a = iv.emplace().index();
  const size_t b = iv.emplace().index();
  pass &= check((a == 3 || a == 5) && (b == 3 || b == 5) && a != b,
      "erased slots are reused before growing");
  pass &= check(iv.capacity() == 8, "capacity after reuse");
  pass &= check(iv.emplace().index() == 8, "growth after reuse");
  pass &= check(addr(iv[7]) == kept, "slot moved on reuse or growth");
  std::printf("%s freed slots are reused\n", pass ? "PASS" : "FAIL");
  return pass;
}

bool testAlignment()
{
  IndexedVector<Storage> iv;
  bool pass = true;
  for (size_t i = 0; i < 1000 && pass; i++) {
    pass &= check(uintptr_t(addr(*iv.emplace())) % alignof(Storage) == 0,
        "slot is not aligned for ISPC");
  }
  std::printf("%s alignment\n", pass ? "PASS" : "FAIL");
  return pass;
}

} // namespace

int main()
{
  bool pass = true;
  pass &= testAddressesSurviveGrowth();
  pass &= testFreedSlotsAreReused();
  pass &= testAlignment();
  return pass ? 0 : 1;
}
