#define Py_LIMITED_API 0x03090000
#include <Python.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct DLDevice {
    int device_type;
    int device_id;
} DLDevice;

typedef struct DLDataType {
    uint8_t code;
    uint8_t bits;
    uint16_t lanes;
} DLDataType;

typedef struct DLTensor {
    void* data;
    DLDevice device;
    int ndim;
    DLDataType dtype;
    int64_t* shape;
    int64_t* strides;
    uint64_t byte_offset;
} DLTensor;

typedef struct DLManagedTensor DLManagedTensor;
struct DLManagedTensor {
    DLTensor dl_tensor;
    void* manager_ctx;
    void (*deleter)(DLManagedTensor* self);
};

static void capsule_destructor(PyObject* capsule) {
    if (!PyCapsule_IsValid(capsule, "dltensor")) return;
    DLManagedTensor* tensor =
        (DLManagedTensor*)PyCapsule_GetPointer(capsule, "dltensor");
    if (tensor != NULL && tensor->deleter != NULL) tensor->deleter(tensor);
}

static PyObject* capsule_from_address(PyObject* self, PyObject* argument) {
    (void)self;
    void* address = PyLong_AsVoidPtr(argument);
    if (address == NULL && PyErr_Occurred()) return NULL;
    if (address == NULL) {
        PyErr_SetString(PyExc_ValueError, "DLManagedTensor address is null");
        return NULL;
    }
    return PyCapsule_New(address, "dltensor", capsule_destructor);
}

typedef struct DLPackOwner {
    DLManagedTensor* tensor;
} DLPackOwner;

static void dlpack_owner_destructor(PyObject* capsule) {
    DLPackOwner* holder =
        (DLPackOwner*)PyCapsule_GetPointer(capsule, "mkvcodec.dlpack_owner");
    if (holder == NULL) {
        PyErr_Clear();
        return;
    }
    if (holder->tensor != NULL && holder->tensor->deleter != NULL)
        holder->tensor->deleter(holder->tensor);
    free(holder);
}

static PyObject* consume_nv12(PyObject* self, PyObject* arguments) {
    (void)self;
    PyObject* provider = NULL;
    unsigned int width = 0;
    unsigned int height = 0;
    if (!PyArg_ParseTuple(arguments, "OII:consume_nv12", &provider, &width, &height))
        return NULL;
    if (width == 0 || height == 0 || (width & 1u) != 0 || (height & 1u) != 0) {
        PyErr_SetString(PyExc_ValueError, "NV12 dimensions must be positive and even");
        return NULL;
    }
    PyObject* source = PyObject_CallMethod(provider, "__dlpack__", NULL);
    if (source == NULL) return NULL;
    if (!PyCapsule_IsValid(source, "dltensor")) {
        Py_DECREF(source);
        PyErr_SetString(PyExc_ValueError, "__dlpack__ did not return a dltensor capsule");
        return NULL;
    }
    DLManagedTensor* managed =
        (DLManagedTensor*)PyCapsule_GetPointer(source, "dltensor");
    if (managed == NULL) {
        Py_DECREF(source);
        return NULL;
    }
    DLTensor* tensor = &managed->dl_tensor;
    const int64_t expected_rows = (int64_t)height + (int64_t)height / 2;
    const int64_t pitch = tensor->strides == NULL ? (int64_t)width : tensor->strides[0];
    const int64_t column_stride = tensor->strides == NULL ? 1 : tensor->strides[1];
    if (tensor->device.device_type != 2 || tensor->device.device_id < 0 ||
        tensor->data == NULL || tensor->ndim != 2 || tensor->shape == NULL ||
        tensor->dtype.code != 1 || tensor->dtype.bits != 8 ||
        tensor->dtype.lanes != 1 || tensor->shape[0] != expected_rows ||
        tensor->shape[1] != (int64_t)width || pitch < (int64_t)width ||
        column_stride != 1) {
        Py_DECREF(source);
        PyErr_SetString(PyExc_ValueError,
            "DLPack NV12 must be CUDA uint8[height*3/2,width] with unit columns");
        return NULL;
    }
    const uintptr_t base = (uintptr_t)tensor->data;
    if (tensor->byte_offset > (uint64_t)(UINTPTR_MAX - base)) {
        Py_DECREF(source);
        PyErr_SetString(PyExc_OverflowError, "DLPack byte offset overflows a pointer");
        return NULL;
    }
    DLPackOwner* holder = (DLPackOwner*)malloc(sizeof(*holder));
    if (holder == NULL) {
        Py_DECREF(source);
        return PyErr_NoMemory();
    }
    holder->tensor = managed;
    if (PyCapsule_SetName(source, "used_dltensor") != 0) {
        free(holder);
        Py_DECREF(source);
        return NULL;
    }
    PyObject* owner = PyCapsule_New(
        holder, "mkvcodec.dlpack_owner", dlpack_owner_destructor);
    Py_DECREF(source);
    if (owner == NULL) {
        if (managed->deleter != NULL) managed->deleter(managed);
        free(holder);
        return NULL;
    }
    PyObject* result = Py_BuildValue(
        "(KiKO)", (unsigned long long)(base + (uintptr_t)tensor->byte_offset),
        tensor->device.device_id, (unsigned long long)pitch, owner);
    Py_DECREF(owner);
    return result;
}

typedef struct ExternalOwner {
    PyObject* owner;
} ExternalOwner;

static void external_owner_release(void* opaque) {
    ExternalOwner* holder = (ExternalOwner*)opaque;
    if (holder == NULL) return;
    if (Py_IsInitialized()) {
        PyGILState_STATE state = PyGILState_Ensure();
        Py_XDECREF(holder->owner);
        PyGILState_Release(state);
    }
    free(holder);
}

static PyObject* external_owner_create(PyObject* self, PyObject* owner) {
    (void)self;
    ExternalOwner* holder = (ExternalOwner*)malloc(sizeof(*holder));
    if (holder == NULL) return PyErr_NoMemory();
    Py_INCREF(owner);
    holder->owner = owner;
    PyObject* user_data = PyLong_FromVoidPtr(holder);
    PyObject* release = PyLong_FromUnsignedLongLong(
        (unsigned long long)(uintptr_t)&external_owner_release);
    if (user_data == NULL || release == NULL) {
        Py_XDECREF(user_data);
        Py_XDECREF(release);
        Py_DECREF(owner);
        free(holder);
        return NULL;
    }
    PyObject* result = PyTuple_Pack(2, user_data, release);
    Py_DECREF(user_data);
    Py_DECREF(release);
    if (result == NULL) {
        Py_DECREF(owner);
        free(holder);
    }
    return result;
}

static PyObject* external_owner_cancel(PyObject* self, PyObject* argument) {
    (void)self;
    ExternalOwner* holder = (ExternalOwner*)PyLong_AsVoidPtr(argument);
    if (holder == NULL && PyErr_Occurred()) return NULL;
    if (holder != NULL) {
        Py_DECREF(holder->owner);
        free(holder);
    }
    Py_RETURN_NONE;
}

static PyMethodDef methods[] = {
    {"capsule_from_address", capsule_from_address, METH_O,
     "Wrap a native DLManagedTensor pointer in an owned DLPack capsule."},
    {"external_owner_create", external_owner_create, METH_O,
     "Retain a Python owner and return native user-data/release addresses."},
    {"external_owner_cancel", external_owner_cancel, METH_O,
     "Cancel an owner holder before native ownership transfer."},
    {"consume_nv12", consume_nv12, METH_VARARGS,
     "Consume a contiguous CUDA NV12 DLPack tensor and return import metadata."},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef module = {
    PyModuleDef_HEAD_INIT, "_dlpack", NULL, -1, methods,
    NULL, NULL, NULL, NULL
};

PyMODINIT_FUNC PyInit__dlpack(void) { return PyModule_Create(&module); }
