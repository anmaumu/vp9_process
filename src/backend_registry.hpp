#pragma once

#include "mkvcodec/mkvc.h"

#include <vector>

namespace mkvc {

/** Return capabilities backed by implementations compiled into this library. */
const std::vector<mkvc_backend_capability>& backend_capabilities();

}  // namespace mkvc
