/**
 * MIT License
 * 
 * Copyright (c) 2017 Loris Cro
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdlib.h>
#include "headers/bit-fiddling.h"
#include "headers/cuckoofilter.h"


// extern inline u64 cf_bucket_size(u32 fpSize, bool isMulti)
// {
//     if (isMulti)
//     {
//         if (fpSize == 1) return 4 + (4 * sizeof(u32));
//         if (fpSize == 2) return 5 * sizeof(u64);
//         return 3 * sizeof(u64);
//     }
//     else
//     {
//         if (fpSize == 1) return 4;
//         return 8;
//     }
// }

/**
 * Computes the alternative bucket location for an FP.
 * Since we are hashing a few bytes (the FP), it uses FNV1a.
 * Also, since numBuckets is a power of 2, it uses a bitwise and
 * instead of a modulo.
 */
extern inline u64 cf_alternative_hash8 (CuckooFilter *cf, u64 hash, u8 fp) {
    return (hash ^ FNV1A(FNV_OFFSET, fp)) & (cf->numBuckets - 1);
}
extern inline u64 cf_alternative_hash16 (CuckooFilter *cf, u64 hash, u16 fp) {
    return (hash ^ FNV1A(FNV1A(FNV_OFFSET, fp & 0xFF), fp >> 8)) & (cf->numBuckets - 1);
}
extern inline u64 cf_alternative_hash32 (CuckooFilter *cf, u64 hash, u32 fp) {
    FP32 fpU = (FP32)fp;
    return (hash ^ FNV1A(FNV1A(FNV1A(FNV1A(FNV_OFFSET, fpU.u8[0]), fpU.u8[1]), fpU.u8[2]), fpU.u8[3])) & (cf->numBuckets - 1);
}

/**
 * What follows is a macronightmare.
 * Each funcion is rewritten for {1, 2, 4}byte fingerprints.
 * The expansion is mainly about u8, u16 and u32 fingerprint types,
 * but there also are some differences in the memory layout.
 * 
 * To inspect the code without losing your mind, preprocess it as follows:
 *     $ gcc -E cuckoofilter.c | astyle > cuckoofilter-exp.c
 *     
 * cuckoofilter-exp.c will then contain a readable version of this file.
 */



/** 
 * Selects the right bucket given a hash by taking into account
 * the bucketSize.
 */
#define CF_READ_BUCKETXX(FPSIZE, BUCKSIZE)\
extern inline u ## FPSIZE *cf_read_bucket ## FPSIZE (CuckooFilter *cf, u64 hash)\
{\
    return (u ## FPSIZE*)(cf->filter + (hash * BUCKSIZE));\
}
CF_READ_BUCKETXX(8, 4);
CF_READ_BUCKETXX(16, 8);
CF_READ_BUCKETXX(32, 8);
#undef CF_READ_BUCKETXX


/**
 * Writes a FP into a free slot. If there are no free slots in the current bucket it
 * either fails or evicts an old FP to make room for the new one, if instructed to 
 * do so. The function calling cf_insert_fp will then handle the re-insertion of the 
 * evicted FP.
 */
#define CF_INSERT_FPXX(FPSIZE, BUCKLEN, WORDSIZE)\
extern inline bool cf_insert_fp ## FPSIZE (CuckooFilter *cf, u64 hash, u ## FPSIZE fp, u ## FPSIZE *former_fp_ptr) {\
    printf("-> %i\n", fp);\
    u ## FPSIZE *bucket = cf_read_bucket ## FPSIZE(cf, hash);\
    if (has_zero ## FPSIZE(*(u ## WORDSIZE*)bucket))\
    {\
        for (int i = 0; i < BUCKLEN; ++i)\
        {\
            if (bucket[i] == 0) {\
               bucket[i] = fp;\
               return 1;\
            }\
        }\
    }\
    if (former_fp_ptr) {\
        int slot = (rand() % BUCKLEN);\
        *former_fp_ptr = bucket[slot];\
        bucket[slot] = fp;\
    }\
    return 0;\
}
CF_INSERT_FPXX(8,  4, 32);
CF_INSERT_FPXX(16, 4, 64);
CF_INSERT_FPXX(32, 2, 64);
#undef CF_INSERT_FPXX


#define CF_DELETE_FPXX(FPSIZE, BUCKLEN, WORDSIZE)\
extern inline bool cf_delete_fp ## FPSIZE (CuckooFilter *cf, u64 hash, u ## FPSIZE fp) {\
    u ## FPSIZE *bucket = cf_read_bucket ## FPSIZE (cf, hash);\
    for (int i = 0; i < BUCKLEN; ++i)\
    {\
        if (bucket[i] == fp) {\
            bucket[i] = 0;\
            return 1;\
        }\
    }\
    bucket = cf_read_bucket ## FPSIZE(cf, cf_alternative_hash ## FPSIZE(cf, hash, fp));\
    for (int i = 0; i < BUCKLEN; ++i)\
    {\
        if (bucket[i] == fp) {\
            bucket[i] = 0;\
            return 1;\
        }\
    }\
    return 0;\
}
CF_DELETE_FPXX(8,  4, 32);
CF_DELETE_FPXX(16, 4, 64);
CF_DELETE_FPXX(32, 2, 64);
#undef CF_DELETE_FPXX


#define CF_SEARCH_FPXX(FPSIZE, WORDSIZE, EXTRA)\
extern inline bool cf_search_fp ## FPSIZE (CuckooFilter *cf, u64 hash, u ## FPSIZE fp){\
    u ## FPSIZE *bucket = cf_read_bucket ## FPSIZE(cf, hash);\
    if (bucket[0] == fp || bucket[1] == fp EXTRA) {\
        return 1;\
    }\
    bucket = cf_read_bucket ## FPSIZE(cf, cf_alternative_hash ## FPSIZE(cf, hash, fp));\
    if (bucket[0] == fp || bucket[1] == fp EXTRA) {\
        return 1;\
    }\
    return 0;\
}
CF_SEARCH_FPXX(8, 32, || bucket[2] == fp || bucket[3] == fp);
CF_SEARCH_FPXX(16, 64, || bucket[2] == fp || bucket[3] == fp);
CF_SEARCH_FPXX(32, 64, );
#undef CF_SEARCH_FPXX

    // if (has_value ## FPSIZE(*(u ## WORDSIZE*)bucket, fp)) {\
    //     return 1;\
    // }\
