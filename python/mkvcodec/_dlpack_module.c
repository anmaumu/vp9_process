#define Py_LIMITED_API 0x03090000
#include <Python.h>

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

static PyMethodDef methods[] = {
    {"capsule_from_address", capsule_from_address, METH_O,
     "Wrap a native DLManagedTensor pointer in an owned DLPack capsule."},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef module = {
    PyModuleDef_HEAD_INIT, "_dlpack", NULL, -1, methods,
    NULL, NULL, NULL, NULL
};

PyMODINIT_FUNC PyInit__dlpack(void) { return PyModule_Create(&module); }
