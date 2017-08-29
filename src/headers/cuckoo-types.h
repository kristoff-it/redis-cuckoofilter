#pragma once

#include <stdint.h>

/** 
 * A couple typedefs to make my life easier.
 */
typedef uint64_t u64;
typedef int64_t  i64;
typedef uint32_t u32;
typedef int32_t  i32;
typedef int32_t  bool;
typedef uint16_t u16;
typedef uint8_t  u8;

typedef struct {
    u64 numBuckets;
    u32 fpSize;
    u32 isMulti;
    u8 *filter;
} CuckooFilter;

typedef union
{
    u16 u16;
    u8  u8[2];
} FP16;

typedef union
{
    u32 u32;
    u16 u16[2];
    u8  u8[4];
} FP32;

