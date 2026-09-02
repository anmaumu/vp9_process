/* GPU-free forwarding / detection test. Each mock returns a sentinel. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define MKVC_AUDIT_API(name, ret, category, params, args) \
    ret name params { return (ret)(uintptr_t)7; }
#include "gpu_copy_audit_api.h"
#undef MKVC_AUDIT_API
void* clGetExtensionFunctionAddressForPlatform(void* platform, const char* name) {
    (void)platform;
#define MKVC_AUDIT_API(symbol, ret, category, params, args) \
    if (strcmp(name, #symbol) == 0) return (void*)&symbol;
#include "gpu_copy_audit_api.h"
#undef MKVC_AUDIT_API
    return NULL;
}
