#pragma once

#include <cstdint>
#include <optional>

namespace cuuid {
	// Hash of the local node's lower-cased name, or nullopt when there is no
	// local node. Default: nullopt (standalone). A host injects the real value.
	inline std::optional<uint64_t> local_node_hash() { return std::nullopt; }
}
