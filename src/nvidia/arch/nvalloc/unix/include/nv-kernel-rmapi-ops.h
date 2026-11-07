/*
 * SPDX-FileCopyrightText: Copyright (c) 2015-2019 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef _NV_KERNEL_RMAPI_OPS_H_
#define _NV_KERNEL_RMAPI_OPS_H_

/*
 * Define the RMAPI provided to kernel-level RM clients.
 *
 * Kernel-level RM clients should populate nvidia_kernel_rmapi_ops_t
 * by assigning nvidia_kernel_rmapi_ops_t::op and the corresponding
 * parameter structure in nvidia_kernel_rmapi_ops_t's params union.
 * Then, pass a pointer to the nvidia_kernel_rmapi_ops_t to
 * rm_kernel_rmapi_op().
 */

#include "nvtypes.h"
#include "nvos.h"

/*
 * NV04_MAP_MEMORY for kernel clients that build a user-space mapping
 * themselves (NVOS33_FLAGS_MEM_SPACE_USER, e.g. nvidia-drm GEM mmap through
 * NVKMS), returning every bus address range of the mapping.
 *
 * A BAR1 mapping of video memory is several ranges when the memory isn't
 * physically contiguous and static BAR1 maps video memory 1:1, or when BAR1
 * is too fragmented for one range. A plain NV04_MAP_MEMORY returns only the
 * start of the first range, so kernel clients are limited to single-range
 * mappings there.
 *
 * pRanges/maxRanges is the caller's buffer. numRanges returns the number of
 * ranges, or 0 if the mapping isn't described by bus address ranges (then
 * only mapMemory.pLinearAddress is valid). If there are more than maxRanges,
 * the mapping is undone, mapMemory.status is NV_ERR_BUFFER_TOO_SMALL and
 * numRanges tells the size needed. Unmap with NV04_UNMAP_MEMORY and
 * mapMemory.pLinearAddress as usual.
 */
#define NV_KERNEL_RMAPI_MAP_MEMORY_RANGES (0x80000000 | NV04_MAP_MEMORY)

typedef struct {
    NvU64 start;    /* bus address */
    NvU64 size;
} nv_kernel_rmapi_range_t;

typedef struct {
    NVOS33_PARAMETERS        mapMemory;     /* IN/OUT, as for NV04_MAP_MEMORY */
    nv_kernel_rmapi_range_t *pRanges;       /* IN */
    NvU32                    maxRanges;     /* IN */
    NvU32                    numRanges;     /* OUT */
} nv_kernel_rmapi_map_memory_ranges_t;

/*
 * Where osMapPciMemoryAreaUser() records the ranges while RM handles
 * NV_KERNEL_RMAPI_MAP_MEMORY_RANGES (TLS_ENTRY_ID_MAP_RANGES).
 */
typedef struct {
    nv_kernel_rmapi_range_t *pRanges;
    NvU32                    maxRanges;
    NvU32                    numRanges;
} NV_KERNEL_RMAPI_RANGE_SINK;

typedef struct {
    NvU32 op; /* One of the NV0[14]_XXXX operations listed below. */

    union {
        NVOS00_PARAMETERS free;                /* NV01_FREE */
        NVOS02_PARAMETERS allocMemory64;       /* NV01_ALLOC_MEMORY */
        NVOS64_PARAMETERS alloc;               /* NV04_ALLOC */
        NVOS32_PARAMETERS *pVidHeapControl;    /* NV04_VID_HEAP_CONTROL */
        NVOS33_PARAMETERS mapMemory;           /* NV04_MAP_MEMORY */
        NVOS34_PARAMETERS unmapMemory;         /* NV04_UNMAP_MEMORY */
        NVOS39_PARAMETERS allocContextDma2;    /* NV04_ALLOC_CONTEXT_DMA */
        NVOS46_PARAMETERS mapMemoryDma;        /* NV04_MAP_MEMORY_DMA */
        NVOS47_PARAMETERS unmapMemoryDma;      /* NV04_UNMAP_MEMORY_DMA */
        NVOS49_PARAMETERS bindContextDma;      /* NV04_BIND_CONTEXT_DMA */
        NVOS54_PARAMETERS control;             /* NV04_CONTROL*/
        NVOS55_PARAMETERS dupObject;           /* NV04_DUP_OBJECT */
        NVOS57_PARAMETERS share;               /* NV04_SHARE */
        NVOS61_PARAMETERS addVblankCallback;   /* NV04_ADD_VBLANK_CALLBACK */
        nv_kernel_rmapi_map_memory_ranges_t mapMemoryRanges; /* NV_KERNEL_RMAPI_MAP_MEMORY_RANGES */
    } params;
} nvidia_kernel_rmapi_ops_t;

#endif /* _NV_KERNEL_RMAPI_OPS_H_ */
