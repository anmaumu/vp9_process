#include "backend_registry.hpp"

namespace mkvc {

const std::vector<mkvc_backend_capability>& backend_capabilities() {
    // Backends register only after their codec dependency has been initialized.
    // An empty list is safer than claiming support based on GPU model alone.
    static const std::vector<mkvc_backend_capability> capabilities;
    return capabilities;
}

}  // namespace mkvc

