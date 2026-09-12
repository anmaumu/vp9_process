#pragma once

/**
 * @file dlpack_types.hpp
 * @brief Minimal DLPack ABI declarations used by the opaque C export.
 *
 * These declarations mirror the stable fields of DLPack's
 * ``DLManagedTensor`` ABI without adding a public dependency on dlpack.h.
 */

#include <cstdint>

enum DLDeviceType : int32_t { kDLCUDA = 2, kDLOneAPI = 14 };

/** DLPack device family and backend-local device ordinal. */
struct DLDevice {
    DLDeviceType device_type; /**< Device API used by the data pointer. */
    int32_t device_id;        /**< Backend-local device ordinal. */
};

/** Scalar element representation of one DLPack tensor. */
struct DLDataType {
    uint8_t code;   /**< DLPack scalar category. */
    uint8_t bits;   /**< Bits in each scalar lane. */
    uint16_t lanes; /**< Vector lanes per element. */
};

/** Strided tensor view over externally owned memory. */
struct DLTensor {
    void* data;           /**< Base device pointer. */
    DLDevice device;      /**< Device owning the pointer. */
    int32_t ndim;         /**< Number of dimensions. */
    DLDataType dtype;     /**< Element type. */
    int64_t* shape;       /**< Extent of each dimension. */
    int64_t* strides;     /**< Element strides for each dimension. */
    uint64_t byte_offset; /**< Byte offset from data to the first element. */
};

/** DLPack tensor plus its single-use ownership callback. */
struct DLManagedTensor {
    DLTensor dl_tensor;                     /**< Borrowed tensor metadata and device pointer. */
    void* manager_ctx;                      /**< Producer-owned lease state. */
    void (*deleter)(DLManagedTensor* self); /**< Releases the producer lease. */
};
