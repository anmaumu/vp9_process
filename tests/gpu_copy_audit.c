/* Independent test instrumentation via glibc LD_AUDIT, including dlsym calls.
 * Not linked into mkvcodec or included in wheel/NuGet. Counts attempted calls,
 * not bytes; unknown maps, private driver calls and GPU-internal copies remain
 * unqualified. https://man7.org/linux/man-pages/man7/rtld-audit.7.html
 */
#define _GNU_SOURCE
#include <link.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static atomic_uint conflicts;
#define MKVC_AUDIT_API(name, ret, category, params, args) \
    static _Atomic(uintptr_t) original_##name; \
    static atomic_ullong calls_##name; \
    static ret wrap_##name params { \
        atomic_fetch_add(&calls_##name, 1); \
        return ((ret (*) params)atomic_load(&original_##name)) args; \
    }
#include "gpu_copy_audit_api.h"
#undef MKVC_AUDIT_API

/* Extension function pointers are returned by this API, not dlsym. */
static _Atomic(uintptr_t) original_extension;
static uintptr_t intercept(const char* name, uintptr_t address);
static void* wrap_extension(void* platform, const char* name) {
    void* address = ((void* (*)(void*, const char*))atomic_load(&original_extension))(platform, name);
    return (void*)intercept(name, (uintptr_t)address);
}

static uintptr_t register_address(_Atomic(uintptr_t)* slot, uintptr_t address, uintptr_t wrapper) {
    uintptr_t expected = 0;
    if (address == wrapper) return wrapper;
    if (!atomic_compare_exchange_strong(slot, &expected, address) && expected != address) {
        /* Multiple implementations cannot share one forwarding slot safely. */
        atomic_fetch_add(&conflicts, 1);
        return address;
    }
    return wrapper;
}

static uintptr_t intercept(const char* name, uintptr_t address) {
    if (!name || !address) return address;
#define MKVC_AUDIT_API(symbol, ret, category, params, args) \
    if (strcmp(name, #symbol) == 0) \
        return register_address(&original_##symbol, address, (uintptr_t)&wrap_##symbol);
#include "gpu_copy_audit_api.h"
#undef MKVC_AUDIT_API
    if (strcmp(name, "clGetExtensionFunctionAddressForPlatform") == 0)
        return register_address(&original_extension, address, (uintptr_t)&wrap_extension);
    return address;
}

unsigned int la_version(unsigned int version) { return version >= LAV_CURRENT ? LAV_CURRENT : 0; }
unsigned int la_objopen(struct link_map* map, Lmid_t lmid, uintptr_t* cookie) {
    (void)map; (void)lmid; (void)cookie;
    return LA_FLG_BINDTO | LA_FLG_BINDFROM;
}
uintptr_t la_symbind64(Elf64_Sym* symbol, unsigned int index,
                     uintptr_t* from, uintptr_t* to, unsigned int* flags, const char* name) {
    (void)index; (void)from; (void)to; (void)flags;
    return intercept(name, symbol->st_value);
}

__attribute__((destructor)) static void report(void) {
    const char* path = getenv("MKVC_GPU_AUDIT_OUTPUT");
    if (!path || path[0] != '/') return;
    FILE* output = fopen(path, "w");
    if (!output) return; /* The parent rejects missing/invalid reports. */
    fprintf(output, "{\"version\":1,\"pid\":%ld,\"binding_conflicts\":%u,\"calls\":{",
            (long)getpid(), atomic_load(&conflicts));
    int separator = 0;
#define MKVC_AUDIT_API(name, ret, category, params, args) \
    fprintf(output, "%s\"%s\":{\"category\":\"%s\",\"bound\":%s,\"count\":%llu}", \
            separator++ ? "," : "", #name, #category, \
            atomic_load(&original_##name) ? "true" : "false", atomic_load(&calls_##name));
#include "gpu_copy_audit_api.h"
#undef MKVC_AUDIT_API
    fputs("}}\n", output);
    fclose(output);
}
