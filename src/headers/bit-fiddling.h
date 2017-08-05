#pragma once

#include "short-types.h"


inline u64 haszero(u64 v) {
    return (v - 0x0101010101010101ULL) & ~v & 0x8080808080808080ULL;
}

inline u64 hasvalue(u64 x, u8 n){
    return haszero(x ^ ~0ULL/255 * n);
}