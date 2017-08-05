#pragma once

#include "short-types.h"

#define CUCKOO_FILTER_ENCODING_VERSION 2
#define BUCKSIZE(fp) ((fp) == 4 ? 2 : 4)
typedef struct {
    u64 numBuckets;
    u32 fpSize;
    bool isMulti;
    u8 *filter;
} CuckooFilter;

extern inline u64 cf_alternative_hash (CuckooFilter *cf, u64 hash, u8 fp);
extern inline u8 *cf_read_bucket (CuckooFilter *cf, u64 hash);
extern inline bool cf_insert_fp (CuckooFilter *cf, u64 hash, u8 fp, u8 *former_fp_ptr);
extern inline bool cf_delete_fp(CuckooFilter *cf, u64 hash, u8 fp);
extern inline bool cf_search_fp(CuckooFilter *cf, u64 hash, u8 fp);
