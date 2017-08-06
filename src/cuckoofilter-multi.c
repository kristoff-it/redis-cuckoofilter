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

#define CUCKOO_FILTER_MULTI_ENCODING_VERSION 0



/** 
 * Multiset Cuckoo filter struct.
 * Assumes that the size of a fingerprint is always 1 byte.
 * The reason for that is that makes the rest of the program
 * easier since, for now, a different FP size seems not that
 * useful for an initial implementation (cf the 2014 paper).
 */
typedef struct {
    u64 numBuckets;
    u64 bytesPerCounter;
    char *filter;
} MultisetCuckooFilter;



/**
 * Creates a new Cuckoo Filter
 */
static inline CuckooFilter *cf_init (u64 size, u32 bucketSize) {
    CuckooFilter *cf = RedisModule_Alloc(sizeof(CuckooFilter));
    cf->numBuckets = size/bucketSize;
    cf->filter = RedisModule_Alloc(size);
    memset(cf->filter, 0, size);
    return cf;
}


/**
 * Computes the alternative bucket location for an FP.
 * Since we are hashing a single byte (the FP), it uses FNV1a.
 * Also, since numBuckets is a power of 2, it uses a bitwise and
 * instead of a modulo.
 */
extern inline u64 mcf_alternative_hash (CuckooFilter *cf, u64 hash, u32 fp) {
    FP32 fpU = (FP32)fp;

    u64 fpH = FNV_OFFSET;
    for (int i = 0; i < cf->fpSize; ++i)
    {
        fpH = FNV1A(fpH, fpU.u8[i]);
    }

    return (hash ^ fpH) % cf->numBuckets;
}


/** 
 * Selects the right bucket given a hash by taking into account
 * the bucketSize.
 */
inline char *cf_read_bucket (CuckooFilter *cf, u64 hash) {
    return cf->filter + (hash * 4);
}

/**
 * Writes a FP into a free slot. If there are no free slots in the current bucket it
 * either fails or evicts an old FP to make room for the new one, if instructed to 
 * do so. The function calling cf_insert_fp will then handle the re-insertion of the 
 * evicted FP.
 */
inline int cf_insert_fp (CuckooFilter *cf, u64 hash, char fp, char *former_fp_ptr) {
    char *bucket = cf_read_bucket(cf, hash);

    if (bucket[0] == 0) {
        bucket[0] = fp;
        return 1;
    }

    if (bucket[1] == 0) {
        bucket[1] = fp;
        return 1;
    }

	if (bucket[2] == 0) {
	    bucket[2] = fp;
	    return 1;
	}

	if (bucket[3] == 0) {
	    bucket[3] = fp;
	    return 1;
	}

    if (former_fp_ptr) {
        int slot = (rand() % 4);
        *former_fp_ptr = bucket[slot];
        bucket[slot] = fp;
    }

    return 0;
}

inline int cf_delete_fp(CuckooFilter *cf, u64 hash, char fp) {
    char *bucket = cf_read_bucket(cf, hash);

    if (bucket[0] == fp) {
        bucket[0] = 0;
        return 1;
    }

    if (bucket[1] == fp) {
        bucket[1] = 0;
        return 1;
    }

	if (bucket[2] == fp) {
	    bucket[2] = 0;
	    return 1;
	}

	if (bucket[3] == fp) {
	    bucket[3] = 0;
	    return 1;
	}

    bucket = cf_read_bucket(cf, cf_alternative_hash(cf, hash, fp));

    if (bucket[0] == fp) {
        bucket[0] = 0;
        return 1;
    }

    if (bucket[1] == fp) {
        bucket[1] = 0;
        return 1;
    }

	if (bucket[2] == fp) {
	    bucket[2] = 0;
	    return 1;
	}

	if (bucket[3] == fp) {
	    bucket[3] = 0;
	    return 1;
	}

    return 0;
}

inline int cf_search_fp(CuckooFilter *cf, u64 hash, char fp){
	char *bucket = cf_read_bucket(cf, hash);

	if (bucket[0] == fp || bucket[1] == fp || bucket[2] == fp || bucket[3] == fp) {
		return 1;
	}

	bucket = cf_read_bucket(cf, cf_alternative_hash(cf, hash, fp));

    if (bucket[0] == fp || bucket[1] == fp || bucket[2] == fp || bucket[3] == fp) {
        return 1;
    }

	return 0;
}

/**
 *
 *  REDIS DATATYPE FUNCTIONS
 *  
 */

void *CFLoad (RedisModuleIO *rdb, int encver)
{

    if (encver != CUCKOO_FILTER_ENCODING_VERSION) {
         // We should actually log an error here, or try to implement
         //   the ability to load older versions of our data structure. 
        return NULL;
    }

    CuckooFilter *cf = RedisModule_Alloc(sizeof(CuckooFilter));
    cf->filter = RedisModule_LoadStringBuffer(rdb, &cf->numBuckets);
    cf->numBuckets /= 4;
    return cf;

}

void CFSave (RedisModuleIO *rdb, void *value)
{
    CuckooFilter *cf = value;
    RedisModule_SaveStringBuffer(rdb, cf->filter, cf->numBuckets * 4);
}

void CFRewrite (RedisModuleIO *aof, RedisModuleString *key, void *value)
{
    printf("CALLED rewrite!\n\n\n");
}

size_t CFMemUsage (void *value)
{

    printf("CALLED mem!\n\n\n");
    return 0;
}

void CFDigest (RedisModuleDigest *digest, void *value)
{

    printf("CALLED digest!\n\n\n");
}

void CFFree (void *cf)
{
    RedisModule_Free(((CuckooFilter*)cf)->filter);
    RedisModule_Free(cf);
    printf("CALLED free!\n\n\n");
}
