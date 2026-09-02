/* Test-only minimal ABI declarations. Linux x86-64, libva / OpenCL 1.2.
 * Opaque objects remain pointers; no vendor header is redistributed.
 * name, return type, observation category, parameters, forwarding arguments.
 */
MKVC_AUDIT_API(vaGetImage, int, host_transfer,
    (void* d, uint32_t s, int x, int y, unsigned w, unsigned h, uint32_t i), (d,s,x,y,w,h,i))
MKVC_AUDIT_API(vaPutImage, int, host_transfer,
    (void* d, uint32_t s, uint32_t i, int x, int y, unsigned w, unsigned h,
     int dx, int dy, unsigned dw, unsigned dh), (d,s,i,x,y,w,h,dx,dy,dw,dh))
MKVC_AUDIT_API(vaDeriveImage, int, metadata,
    (void* d, uint32_t s, void* i), (d,s,i))
MKVC_AUDIT_API(vaMapBuffer, int, unclassified_map,
    (void* d, uint32_t b, void** p), (d,b,p))
MKVC_AUDIT_API(vaMapBuffer2, int, unclassified_map,
    (void* d, uint32_t b, void** p, uint32_t flags), (d,b,p,flags))
MKVC_AUDIT_API(clEnqueueReadBuffer, int, host_transfer,
    (void* q, void* b, unsigned wait, size_t offset, size_t bytes, void* p,
     unsigned n, const void* events, void* event), (q,b,wait,offset,bytes,p,n,events,event))
MKVC_AUDIT_API(clEnqueueWriteBuffer, int, host_transfer,
    (void* q, void* b, unsigned wait, size_t offset, size_t bytes, const void* p,
     unsigned n, const void* events, void* event), (q,b,wait,offset,bytes,p,n,events,event))
MKVC_AUDIT_API(clEnqueueReadImage, int, host_transfer,
    (void* q, void* i, unsigned wait, const size_t* origin, const size_t* region,
     size_t row, size_t slice, void* p, unsigned n, const void* events, void* event),
    (q,i,wait,origin,region,row,slice,p,n,events,event))
MKVC_AUDIT_API(clEnqueueWriteImage, int, host_transfer,
    (void* q, void* i, unsigned wait, const size_t* origin, const size_t* region,
     size_t row, size_t slice, const void* p, unsigned n, const void* events, void* event),
    (q,i,wait,origin,region,row,slice,p,n,events,event))
MKVC_AUDIT_API(clEnqueueMapBuffer, void*, host_map,
    (void* q, void* b, unsigned wait, uint64_t flags, size_t offset, size_t bytes,
     unsigned n, const void* events, void* event, int* error),
    (q,b,wait,flags,offset,bytes,n,events,event,error))
MKVC_AUDIT_API(clEnqueueMapImage, void*, host_map,
    (void* q, void* i, unsigned wait, uint64_t flags, const size_t* origin,
     const size_t* region, size_t* row, size_t* slice, unsigned n,
     const void* events, void* event, int* error),
    (q,i,wait,flags,origin,region,row,slice,n,events,event,error))
MKVC_AUDIT_API(clEnqueueNDRangeKernel, int, kernel,
    (void* q, void* k, unsigned dim, const size_t* offset, const size_t* global,
     const size_t* local, unsigned n, const void* events, void* event),
    (q,k,dim,offset,global,local,n,events,event))
MKVC_AUDIT_API(clEnqueueAcquireVA_APIMediaSurfacesINTEL, int, sharing,
    (void* q, unsigned count, const void* objects, unsigned n, const void* events, void* event),
    (q,count,objects,n,events,event))
MKVC_AUDIT_API(clEnqueueReleaseVA_APIMediaSurfacesINTEL, int, sharing,
    (void* q, unsigned count, const void* objects, unsigned n, const void* events, void* event),
    (q,count,objects,n,events,event))
