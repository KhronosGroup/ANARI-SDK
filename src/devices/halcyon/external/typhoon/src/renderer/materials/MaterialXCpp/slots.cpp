//
// MaterialXCpp slot identifiers for hot-path parameter lookups.
//
#include "slots.h"

#include <mutex>
#include <unordered_map>

namespace mxcpp {

namespace {

struct SlotRegistry {
    std::mutex mutex;
    std::unordered_map<std::string, SlotId> ids;
    SlotId nextId = 0;
};

SlotRegistry&
_GetSlotRegistry()
{
    static SlotRegistry registry;
    return registry;
}

}  // anonymous namespace

SlotId
InternSlot(const std::string& name)
{
    auto& registry = _GetSlotRegistry();
    std::lock_guard<std::mutex> lock(registry.mutex);

    auto it = registry.ids.find(name);
    if (it != registry.ids.end()) {
        return it->second;
    }

    const SlotId slot = registry.nextId++;
    registry.ids.emplace(name, slot);
    return slot;
}

SlotId
InternSlot(const char* name)
{
    return InternSlot(std::string(name));
}

SlotId
SlotName::Get() const
{
    SlotId slot = _slot.load(std::memory_order_acquire);
    if (slot != InvalidSlotId) {
        return slot;
    }

    // Multiple threads may race here and each call InternSlot(), but
    // InternSlot is idempotent for the same name (returns the same SlotId),
    // so storing twice is harmless.
    slot = InternSlot(_name);
    _slot.store(slot, std::memory_order_release);
    return slot;
}

}  // namespace mxcpp
