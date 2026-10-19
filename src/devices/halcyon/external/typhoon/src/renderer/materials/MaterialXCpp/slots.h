//
// MaterialXCpp slot identifiers for hot-path parameter lookups.
//
#ifndef MXCPP_SLOTS_H
#define MXCPP_SLOTS_H

#include <atomic>
#include <cstdint>
#include <string>

namespace mxcpp {

using SlotId = uint32_t;
constexpr SlotId InvalidSlotId = ~SlotId(0);

SlotId InternSlot(const std::string& name);
SlotId InternSlot(const char* name);

class SlotName
{
public:
    explicit SlotName(const char* name)
        : _name(name)
        , _slot(InvalidSlotId)
    {
    }

    SlotId Get() const;
    const char* GetText() const { return _name; }

private:
    const char* _name;
    mutable std::atomic<SlotId> _slot;
};

inline SlotId
AsSlotId(SlotId slot)
{
    return slot;
}

inline SlotId
AsSlotId(const SlotName& slot)
{
    return slot.Get();
}

inline SlotId
AsSlotId(const char* name)
{
    return InternSlot(name);
}

inline SlotId
AsSlotId(const std::string& name)
{
    return InternSlot(name);
}

}  // namespace mxcpp

#endif  // MXCPP_SLOTS_H
