"""Test-only DMA-BUF layout validation for a future safe Intel USM adapter.

ABI: https://github.com/intel/libva/blob/2.23.0/va/va_drmcommon.h
Exporting an fd does not linearize tiled/compressed pixels or establish SYCL ownership.
"""
import ctypes as ct
import os
from intel_va_opencl_support import bind, check, P, U, I


class Object(ct.Structure):
    _fields_ = [("fd", I), ("size", U), ("modifier", ct.c_uint64)]


class Layer(ct.Structure):
    _fields_ = [("format", U), ("planes", U), ("objects", U * 4), ("offsets", U * 4), ("pitches", U * 4)]


class Prime(ct.Structure):
    _fields_ = [("fourcc", U), ("width", U), ("height", U), ("num_objects", U),
                ("objects", Object * 4), ("num_layers", U), ("layers", Layer * 4)]


class ValueData(ct.Union):
    _fields_ = [("i", I), ("f", ct.c_float), ("p", P), ("fn", P)]


class Value(ct.Structure):
    _fields_ = [("type", I), ("value", ValueData)]


class Attribute(ct.Structure):
    _fields_ = [("type", I), ("flags", U), ("value", Value)]


class Modifiers(ct.Structure):
    _fields_ = [("count", U), ("modifiers", ct.POINTER(ct.c_uint64))]


def linear_attributes():
    modifiers = (ct.c_uint64 * 1)(0)  # DRM_FORMAT_MOD_LINEAR only.
    descriptor = Modifiers(1, modifiers)
    attrs = (Attribute * 1)()
    attrs[0].type, attrs[0].flags, attrs[0].value.type = 9, 2, 3
    attrs[0].value.value.p = ct.addressof(descriptor)
    return attrs, descriptor, modifiers  # Keep pointees alive during vaCreateSurfaces.


def describe(prime):
    if not 1 <= prime.num_objects <= 4 or not 1 <= prime.num_layers <= 4:
        raise ValueError("Invalid DRM object/layer counts")
    objects = [{"size": o.size, "modifier": o.modifier} for o in prime.objects[:prime.num_objects]]
    layers = []
    for layer in prime.layers[:prime.num_layers]:
        if not 1 <= layer.planes <= 4:
            raise ValueError("Invalid DRM plane count")
        planes = []
        for index in range(layer.planes):
            obj = layer.objects[index]
            if obj >= prime.num_objects or layer.offsets[index] >= objects[obj]["size"] or not layer.pitches[index]:
                raise ValueError("Invalid DRM plane layout")
            planes.append({"object": obj, "offset": layer.offsets[index], "pitch": layer.pitches[index]})
        layers.append({"format": layer.format, "planes": planes})
    return {"fourcc": prime.fourcc, "width": prime.width, "height": prime.height,
            "objects": objects, "layers": layers,
            "linear_modifier": all(obj["modifier"] == 0 for obj in objects)}


def export_layout(display, surface):
    """Metadata-only export; close each newly exported fd exactly once."""
    va = ct.CDLL("libva.so.2")
    export = bind(va, "vaExportSurfaceHandle", I, P, U, U, U, P)
    prime = Prime()
    for obj in prime.objects:
        obj.fd = -1
    check(export(display, surface, 0x40000000, 1 | 4, ct.byref(prime)))
    try:
        result = describe(prime)
        for index, obj in enumerate(prime.objects[:prime.num_objects]):
            stat = os.fstat(obj.fd)
            result["objects"][index]["dma_identity"] = [stat.st_dev, stat.st_ino]
        return result
    finally:
        closed = set()
        for obj in prime.objects[:min(prime.num_objects, 4)]:
            if obj.fd >= 0 and obj.fd not in closed:
                os.close(obj.fd)
                closed.add(obj.fd)
