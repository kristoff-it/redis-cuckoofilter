#define CUCKOO_FILTER_ENCODING_VERSION 0

/** 
 * A couple typedefs to make my life easier.
 */
typedef uint64_t u64;
typedef int64_t  i64;
typedef uint32_t u32;

/** 
 * Cuckoo filter struct.
 * Assumes that the size of a fingerprint is always 1 byte.
 * The reason for that is that makes the rest of the program
 * easier since, for now, a different FP size seems not that
 * useful for an initial implementation (cf the 2014 paper).
 */
typedef struct {
    u64 numBuckets;
    u64 bucketSize;
    char *filter;
} CuckooFilter;


/**
 * Creates a new Cuckoo Filter
 */
inline CuckooFilter *cf_init(u64 size, u32 bucketSize) {
    CuckooFilter *cf = RedisModule_Alloc(sizeof(CuckooFilter));
    cf->numBuckets = size/bucketSize;
    cf->bucketSize = bucketSize;
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
inline u64 cf_alternative_hash(CuckooFilter *cf, u64 hash, char fp) {
    const u64 fnv_offset = 14695981039346656037ULL;
    const u64 fnv_prime = 1099511628211ULL;

    //             ( ------ FNV1a hashing ------ )
    return (hash ^ ((fnv_offset ^ fp) * fnv_prime)) & (cf->numBuckets - 1);
}


/** 
 * Selects the right bucket given a hash by taking into account
 * the bucketSize.
 */
inline char *cf_read_bucket (CuckooFilter *cf, u64 hash) {
    return cf->filter + (hash * cf->bucketSize);
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

    if (cf->bucketSize == 4) {
    	if (bucket[2] == 0) {
    	    bucket[2] = fp;
    	    return 1;
    	}

    	if (bucket[3] == 0) {
    	    bucket[3] = fp;
    	    return 1;
    	}
    }

    if (former_fp_ptr) {
        int slot = (rand() % cf->bucketSize);
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

    if (cf->bucketSize == 4) {
    	if (bucket[2] == fp) {
    	    bucket[2] = 0;
    	    return 1;
    	}

    	if (bucket[3] == fp) {
    	    bucket[3] = 0;
    	    return 1;
    	}
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

    if (cf->bucketSize == 4) {
    	if (bucket[2] == fp) {
    	    bucket[2] = 0;
    	    return 1;
    	}

    	if (bucket[3] == fp) {
    	    bucket[3] = 0;
    	    return 1;
    	}
    }

    return 0;
}

inline int cf_search_fp(CuckooFilter *cf, u64 hash, char fp){
	char *bucket = cf_read_bucket(cf, hash);

	if (bucket[0] == fp || bucket[1] == fp) {
		return 1;
	}


	if (cf->bucketSize == 4) {
		if (bucket[2] == fp || bucket[3] == fp) {
			return 1;
		}
	}


	bucket = cf_read_bucket(cf, cf_alternative_hash(cf, hash, fp));

	if (bucket[0] == fp || bucket[1] == fp) {
		return 1;
	}


	if (cf->bucketSize == 4) {
		if (bucket[2] == fp || bucket[3] == fp) {
			return 1;
		}
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
    cf->bucketSize = RedisModule_LoadUnsigned(rdb);
    cf->filter = RedisModule_LoadStringBuffer(rdb, &cf->numBuckets);
    cf->numBuckets /= cf->bucketSize;
    return cf;

}

void CFSave (RedisModuleIO *rdb, void *value)
{
    CuckooFilter *cf = value;
    RedisModule_SaveUnsigned(rdb, cf->bucketSize);
    RedisModule_SaveStringBuffer(rdb, cf->filter, cf->numBuckets * cf->bucketSize);
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
