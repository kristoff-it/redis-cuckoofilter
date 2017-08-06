#pragma once

#include "cuckoo-types.h"

#define CUCKOO_FILTER_ENCODING_VERSION 2

#define HEADERS(FPSIZE)\
extern inline u64  cf_alternative_hash ## FPSIZE (CuckooFilter *cf, u64 hash, u ## FPSIZE fp);\
extern inline u ## FPSIZE  *cf_read_bucket ## FPSIZE (CuckooFilter *cf, u64 hash);\
extern inline bool cf_insert_fp ## FPSIZE (CuckooFilter *cf, u64 hash, u ## FPSIZE fp, u ## FPSIZE *former_fp_ptr);\
extern inline bool cf_delete_fp ## FPSIZE (CuckooFilter *cf, u64 hash, u ## FPSIZE fp);\
extern inline bool cf_search_fp ## FPSIZE (CuckooFilter *cf, u64 hash, u ## FPSIZE fp);
HEADERS(8)
HEADERS(16)
HEADERS(32)
#undef HEADERS