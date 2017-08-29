#pragma once

#include "cuckoo-types.h"

#define FNV_OFFSET 14695981039346656037ULL
#define FNV_PRIME 1099511628211ULL
#define FNV1A(h, x) (((h) ^ (x)) * FNV_PRIME)


// TODO: probably hasvalue32 is useless and a direct comparison would be better

static inline u32 has_zero8(u32 v) {
    return (v - 0x01010101UL) & ~v & 0x80808080UL;
}
static inline u64 has_zero16(u64 v) {
    return (v - 0x0100010001000100ULL) & ~v & 0x8000800080008000ULL;
}
static inline u64 has_zero32(u64 v) {
    return (v - 0x0100000001000000ULL) & ~v & 0x8000000080000000ULL;
}


static inline u32 has_value8(u32 x, u8 n){
    return has_zero8(x ^ ~0UL/255 * n);
}
static inline u64 has_value16(u64 x, u16 n){
    return has_zero16(x ^ ~0ULL/65535 * n);
}
static inline u64 has_value32(u64 x, u32 n){
    return has_zero32(x ^ ~0ULL/4294967295 * n);
}
