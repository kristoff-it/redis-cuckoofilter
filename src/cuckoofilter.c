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

#include "headers/bit-fiddling.h"
#include "headers/cuckoofilter.h"


/**
 * Computes the alternative bucket location for an FP.
 * Since we are hashing a single byte (the FP), it uses FNV1a.
 * Also, since numBuckets is a power of 2, it uses a bitwise and
 * instead of a modulo.
 */
extern inline u64 cf_alternative_hash (CuckooFilter *cf, u64 hash, u8 fp) {
    const u64 fnv_offset = 14695981039346656037ULL;
    const u64 fnv_prime = 1099511628211ULL;

    //             ( ------ FNV1a hashing ------ )
    return (hash ^ ((fnv_offset ^ fp) * fnv_prime)) & (cf->numBuckets - 1);
}


/** 
 * Selects the right bucket given a hash by taking into account
 * the bucketSize.
 */
extern inline u8 *cf_read_bucket (CuckooFilter *cf, u64 hash) {
    return cf->filter + (hash * 8);
}

/**
 * Writes a FP into a free slot. If there are no free slots in the current bucket it
 * either fails or evicts an old FP to make room for the new one, if instructed to 
 * do so. The function calling cf_insert_fp will then handle the re-insertion of the 
 * evicted FP.
 */
extern inline bool cf_insert_fp (CuckooFilter *cf, u64 hash, u8 fp, u8 *former_fp_ptr) {
    u8 *bucket = cf_read_bucket(cf, hash);

    if (haszero(*(u64*)bucket))
    {
        for (int i = 0; i < 8; ++i)
        {
            if (bucket[i] == 0) {
               bucket[i] = fp;
               return 1;
            }
        }
    }

    if (former_fp_ptr) {
        int slot = (rand() % 8);
        *former_fp_ptr = bucket[slot];
        bucket[slot] = fp;
    }

    return 0;
}

extern inline bool cf_delete_fp(CuckooFilter *cf, u64 hash, u8 fp) {
    u8 *bucket = cf_read_bucket(cf, hash);

    for (int i = 0; i < 8; ++i)
    {
        if (bucket[i] == fp) {
            bucket[i] = 0;
            return 1;
        }
    }


    bucket = cf_read_bucket(cf, cf_alternative_hash(cf, hash, fp));

    for (int i = 0; i < 8; ++i)
    {
        if (bucket[i] == fp) {
            bucket[i] = 0;
            return 1;
        }
    }


    return 0;
}




extern inline bool cf_search_fp(CuckooFilter *cf, u64 hash, u8 fp){
    u64 *bucket = (u64*)(cf->filter + (hash * 8));

    if (hasvalue(*bucket, fp)){
        return 1;
    }

    bucket = (u64*)(cf->filter + (cf_alternative_hash(cf, hash, fp) * 8));

    if (hasvalue(*bucket, fp)){
        return 1;
    }

    return 0;
}

