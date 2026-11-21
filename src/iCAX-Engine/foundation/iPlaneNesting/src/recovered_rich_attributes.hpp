#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <vector>

namespace study::recovered {
// Rich geometry attributes supply the same two operations as the original
// attribute vtable. A clone may be empty, and repeated source instances may
// request a shared clone within one copy operation.
class RichAttribute {
public:
    virtual ~RichAttribute() = default;
    virtual bool sharedClone() const = 0;
    virtual std::shared_ptr<const RichAttribute> clone() const = 0;
};
using RichAttributes = std::vector<std::shared_ptr<const RichAttribute>>;

class RichAttributeCopies {
public:
    explicit RichAttributeCopies(bool reuseSource = false) : reuseSource_(reuseSource) {}
    std::shared_ptr<const RichAttribute> copy(const std::shared_ptr<const RichAttribute> &);
private:
    bool reuseSource_;
    std::map<std::shared_ptr<const RichAttribute>, std::shared_ptr<const RichAttribute>> copies_;
};

void richCopyAttributes(RichAttributes &destination, std::span<const std::shared_ptr<const RichAttribute>> source,
                        RichAttributeCopies &); // b75870 / b75770 / b75ae0

struct RichSourceAttributes {
    uint32_t sequence = 0;
    std::map<int32_t, RichAttributes> entries;
    int32_t insert(const RichAttributes &); // bed440, before segment-length filtering
};
}
