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
static atomic_uint runtime_objects;
static atomic_uint objects;
#define SLOTS(M, name, ret, category, params, args) \
    M(0,name,ret,category,params,args) M(1,name,ret,category,params,args) \
    M(2,name,ret,category,params,args) M(3,name,ret,category,params,args) \
    M(4,name,ret,category,params,args) M(5,name,ret,category,params,args) \
    M(6,name,ret,category,params,args) M(7,name,ret,category,params,args) \
    M(8,name,ret,category,params,args) M(9,name,ret,category,params,args) \
    M(10,name,ret,category,params,args) M(11,name,ret,category,params,args) \
    M(12,name,ret,category,params,args) M(13,name,ret,category,params,args) \
    M(14,name,ret,category,params,args) M(15,name,ret,category,params,args)
#define WRAP(n, name, ret, category, params, args) \
    static ret wrap_##name##n params { \
        atomic_fetch_add(&calls_##name, 1); \
        return ((ret (*) params)atomic_load(&original_##name[n])) args; \
    }
#define ADDRESS(n, name, ret, category, params, args) (uintptr_t)&wrap_##name##n,
#define MKVC_AUDIT_API(name, ret, category, params, args) \
    static _Atomic(uintptr_t) original_##name[16]; \
    static atomic_ullong calls_##name; \
    SLOTS(WRAP,name,ret,category,params,args) \
    static const uintptr_t wrappers_##name[16] = {SLOTS(ADDRESS,name,ret,category,params,args)};
#include "gpu_copy_audit_api.h"
#undef MKVC_AUDIT_API

/* Extension function pointers are returned by this API, not dlsym. */
static uintptr_t intercept(const char* name, uintptr_t address);
static _Atomic(uintptr_t) original_extension[16];
#define EXTENSION(n, symbol, ret, category, params, args) \
    static void* wrap_extension##n(void* platform, const char* name) { \
        void* address = ((void* (*)(void*, const char*))atomic_load(&original_extension[n]))(platform, name); \
        return (void*)intercept(name, (uintptr_t)address); \
    }
SLOTS(EXTENSION,extension,void*,unused,(),())
static const uintptr_t wrappers_extension[16] = {SLOTS(ADDRESS,extension,void*,unused,(),())};

static uintptr_t register_address(_Atomic(uintptr_t)* slots, uintptr_t address, const uintptr_t* wrappers) {
    for (int i = 0; i < 16; ++i) {
        if (address == wrappers[i]) return address;
    }
    for (int i = 0; i < 16; ++i) {
        uintptr_t expected = 0;
        if (atomic_compare_exchange_strong(&slots[i], &expected, address) || expected == address)
            return wrappers[i];
    }
    /* Namespace-isolated DSOs need distinct forwarding targets. Fail closed
     * if the bounded table is exhausted; never forward into a different DSO. */
    atomic_fetch_add(&conflicts, 1);
    return address;
}

static uintptr_t intercept(const char* name, uintptr_t address) {
    if (!name || !address) return address;
#define MKVC_AUDIT_API(symbol, ret, category, params, args) \
    if (strcmp(name, #symbol) == 0) \
        return register_address(original_##symbol, address, wrappers_##symbol);
#include "gpu_copy_audit_api.h"
#undef MKVC_AUDIT_API
    if (strcmp(name, "clGetExtensionFunctionAddressForPlatform") == 0)
        return register_address(original_extension, address, wrappers_extension);
    return address;
}

unsigned int la_version(unsigned int version) { return version >= LAV_CURRENT ? LAV_CURRENT : 0; }
unsigned int la_objopen(struct link_map* map, Lmid_t lmid, uintptr_t* cookie) {
    (void)lmid; (void)cookie;
    atomic_fetch_add(&objects, 1);
    if (map->l_name && strstr(map->l_name, "libmfx-gen")) atomic_fetch_add(&runtime_objects, 1);
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
            atomic_load(&original_##name[0]) ? "true" : "false", atomic_load(&calls_##name));
#include "gpu_copy_audit_api.h"
#undef MKVC_AUDIT_API
    fprintf(output, "},\"coverage\":{\"scope\":\"exported_api_only\",\"objects\":%u,\"vpl_runtime_objects\":%u}}\n",
            atomic_load(&objects), atomic_load(&runtime_objects));
    fclose(output);
}
