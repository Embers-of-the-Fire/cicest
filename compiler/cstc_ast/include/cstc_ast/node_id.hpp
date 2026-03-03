#ifndef CICEST_COMPILER_CSTC_AST_NODE_ID_HPP
#define CICEST_COMPILER_CSTC_AST_NODE_ID_HPP

#include <cstdint>

namespace cstc::ast {

/// Unique identifier for every AST node.
struct NodeId {
    std::uint32_t id;

    [[nodiscard]] bool operator==(const NodeId& rhs) const noexcept {
        return id == rhs.id;
    }

    [[nodiscard]] bool operator!=(const NodeId& rhs) const noexcept {
        return id != rhs.id;
    }
};

/// Sentinel value for uninitialized or placeholder nodes.
inline constexpr NodeId DUMMY_NODE_ID{0};

/// Monotonically-increasing allocator for NodeId values.
class NodeIdAllocator {
    std::uint32_t next_ = 1; // 0 is reserved for DUMMY_NODE_ID

public:
    [[nodiscard]] NodeId next() noexcept { return NodeId{next_++}; }
};

} // namespace cstc::ast

#endif // CICEST_COMPILER_CSTC_AST_NODE_ID_HPP
