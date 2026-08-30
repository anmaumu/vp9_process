#define Py_LIMITED_API 0x03090000
#include <Python.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct DLManagedTensor DLManagedTensor;
struct DLManagedTensor {
    unsigned char opaque_tensor[48];
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
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef module = {
    PyModuleDef_HEAD_INIT, "_dlpack", NULL, -1, methods,
    NULL, NULL, NULL, NULL
};

PyMODINIT_FUNC PyInit__dlpack(void) { return PyModule_Create(&module); }
