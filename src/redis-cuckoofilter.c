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
#include <string.h>
#include <math.h>
#include "redismodule.h"
#include "headers/cuckoofilter.h"
// #include "tempfilter.c"

static RedisModuleType *CuckooFilterType;

/**
 * Creates a new Cuckoo Filter
 */
static inline CuckooFilter *cf_init(u64 size, u64 fpSize, bool isMulti) {
    CuckooFilter *cf = RedisModule_Alloc(sizeof(CuckooFilter));
    
    cf->numBuckets = size/(fpSize == 1 ? 4 : 8);
    cf->fpSize = (u32)fpSize;
    cf->isMulti = (u32)isMulti;
    cf->filter = RedisModule_Alloc(size);

    memset(cf->filter, 0, size);
    return cf;
}




/** 
 * CF.INIT key numBuckets [fpSize]
 *
 * numBuckets is a power of 2 in the range:
 *     [1K, ..., 512K, ..., 1M, ..., 512M, ..., 1G, ...,  8G]
 *     
 * fpSize is {1, 2, 4}. Defaults to 1.
 * Bucket size changes in function of fpSize: 
 *     {1, 2} -> 4
 *     {4}    -> 2.
 */
int CFInit_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    RedisModule_AutoMemory(ctx);
    if (argc != 3 && argc != 4) return RedisModule_WrongArity(ctx);

    const char *filterType = RedisModule_StringPtrLen(argv[2], NULL);
    
    /**
     *  Parse filterType and compute the corresponding bytesize.
     *  TODO: gperf
     */
    u64 size = 1024ULL;
    if      (0 == strcmp("1K",   filterType)) size *= 1;
    else if (0 == strcmp("2K",   filterType)) size *= 2;
    else if (0 == strcmp("4K",   filterType)) size *= 4;
    else if (0 == strcmp("8K",   filterType)) size *= 8;
    else if (0 == strcmp("16K",  filterType)) size *= 16;
    else if (0 == strcmp("32K",  filterType)) size *= 32;
    else if (0 == strcmp("64K",  filterType)) size *= 64;
    else if (0 == strcmp("128K", filterType)) size *= 128;
    else if (0 == strcmp("256K", filterType)) size *= 256;
    else if (0 == strcmp("512K", filterType)) size *= 512;
    else if (0 == strcmp("1M",   filterType)) size *= 1024;
    else if (0 == strcmp("2M",   filterType)) size *= 1024 * 2;
    else if (0 == strcmp("4M",   filterType)) size *= 1024 * 4;
    else if (0 == strcmp("8M",   filterType)) size *= 1024 * 8;
    else if (0 == strcmp("16M",  filterType)) size *= 1024 * 16;
    else if (0 == strcmp("32M",  filterType)) size *= 1024 * 32;
    else if (0 == strcmp("64M",  filterType)) size *= 1024 * 64;
    else if (0 == strcmp("128M", filterType)) size *= 1024 * 128;
    else if (0 == strcmp("256M", filterType)) size *= 1024 * 256;
    else if (0 == strcmp("512M", filterType)) size *= 1024 * 512;
    else if (0 == strcmp("1G",   filterType)) size *= 1024 * 1024;
    else if (0 == strcmp("2G",   filterType)) size *= 1024 * 1024 * 2;
    else if (0 == strcmp("4G",   filterType)) size *= 1024 * 1024 * 4;
    else if (0 == strcmp("8G",   filterType)) size *= 1024 * 1024 * 8;
    else {
        RedisModule_ReplyWithError(ctx,"ERR unsupported filter size");
        return REDISMODULE_OK;
    }

    /**
     * Parse fpSize
     */
    i64 fpSize = 1;
    if (argc == 4)
    {
        if (RedisModule_StringToLongLong(argv[3], &fpSize) != REDISMODULE_OK) 
        {
            RedisModule_ReplyWithError(ctx,"ERR invalid fingerprint size value");
            return REDISMODULE_OK;
        }

        if (fpSize != 1 && fpSize != 2 && fpSize != 4)
        {
            RedisModule_ReplyWithError(ctx,"ERR unsupported fingerprint size");
            return REDISMODULE_OK;
        }
    }

    /**
     * Obtain the key from Redis.
     */
    RedisModuleKey *key = RedisModule_OpenKey(ctx, argv[1], REDISMODULE_READ|REDISMODULE_WRITE);
    int type = RedisModule_KeyType(key);
    if (type != REDISMODULE_KEYTYPE_EMPTY && RedisModule_ModuleTypeGetType(key) != CuckooFilterType)
    {
        RedisModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
        return REDISMODULE_OK;
    }

    CuckooFilter *cf;

    if (type == REDISMODULE_KEYTYPE_EMPTY) {

        /** 
         * New Cuckoo Filter!
         */
        cf = cf_init(size, fpSize, 0);

        RedisModule_ModuleTypeSetValue(key, CuckooFilterType, cf);
    } else {
        RedisModule_ReplyWithError(ctx,"ERR key already exists");
        return REDISMODULE_OK;
    }

    RedisModule_ReplyWithLongLong(ctx, size/fpSize);
    return REDISMODULE_OK;
}


/**
 * Terrible macro that does the setup part for cd.add, cf.rem, and cf.check.
 * Parses the hash, the FP, and gets the CuckooFilter from Redis.
 * Since handling errors is gonna be somewhat janky anyway, I'm going full macro.
 * (for now)
 */
#define COMMAND_4_PREAMBLE(mode) {\
    RedisModule_AutoMemory(ctx);\
    if (argc != 4) return RedisModule_WrongArity(ctx);\
    \
    if (RedisModule_StringToLongLong(argv[2], (i64*)&hash) != REDISMODULE_OK) {\
        RedisModule_ReplyWithError(ctx,"ERR hash is not unsigned long long");\
        return REDISMODULE_OK;\
    }\
    if (RedisModule_StringToLongLong(argv[3], (i64*)&fpLong) != REDISMODULE_OK) {\
        RedisModule_ReplyWithError(ctx,"ERR invalid fprint value");\
        return REDISMODULE_OK;\
    }\
    key = RedisModule_OpenKey(ctx, argv[1], mode);\
    int type = RedisModule_KeyType(key);\
    if (type == REDISMODULE_KEYTYPE_EMPTY || RedisModule_ModuleTypeGetType(key) != CuckooFilterType)\
    {\
        RedisModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);\
        return REDISMODULE_OK;\
    }\
    cf = (CuckooFilter*)RedisModule_ModuleTypeGetValue(key);\
    switch(cf->fpSize)\
    {\
        case 1:\
            if ((fpLong & 0xff) == 0)\
            {\
               fpLong = 1;\
            }\
            break;\
        case 2:\
            if ((fpLong & 0xffff) == 0)\
            {\
               fpLong = 1;\
            }\
            break;\
        case 4:\
            if ((fpLong & 0xffffffff) == 0)\
            {\
               fpLong = 1;\
            }\
            break;\
    }\
    hash = hash & (cf->numBuckets - 1);\
}

/**
 * CF.ADD key bucket fp
 */
int CFAdd_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    u64 hash;
    RedisModuleKey *key;
    u64 fpLong;
    CuckooFilter *cf;
    COMMAND_4_PREAMBLE(REDISMODULE_READ|REDISMODULE_WRITE)

    switch(cf->fpSize)
    {
        case 1:
        {
            u8 fp = fpLong & 0xff;
            u64 altHash = cf_alternative_hash8(cf, hash, fp);
            if (cf_insert_fp8(cf, hash, fp, NULL) || cf_insert_fp8(cf, altHash, fp, NULL)) 
            {
                RedisModule_ReplyWithSimpleString(ctx, "OK");
                return REDISMODULE_OK;
            }

            u8 homelessFP = 0;
            u64 homelessHash = altHash;
            for (int n = 0; n < 500; n++) {
                cf_insert_fp8(cf, homelessHash, fp, &homelessFP);

                if (!homelessFP) {
                    RedisModule_ReplyWithSimpleString(ctx, "OK");
                    return REDISMODULE_OK;
                } 

                homelessHash = cf_alternative_hash8(cf, homelessHash, homelessFP);
                fp = homelessFP;
                homelessFP = 0;
            }
        } break;
        case 2:
        {
            u16 fp = fpLong & 0xffff;
            u64 altHash = cf_alternative_hash16(cf, hash, fp);
            if (cf_insert_fp16(cf, hash, fp, NULL) || cf_insert_fp16(cf, altHash, fp, NULL)) 
            {
                RedisModule_ReplyWithSimpleString(ctx, "OK");
                return REDISMODULE_OK;
            }

            u16 homelessFP = 0;
            u64 homelessHash = altHash;
            for (int n = 0; n < 500; n++) {

                cf_insert_fp16(cf, homelessHash, fp, &homelessFP);

                if (!homelessFP) {
                    RedisModule_ReplyWithSimpleString(ctx, "OK");
                    return REDISMODULE_OK;
                }

                homelessHash = cf_alternative_hash16(cf, homelessHash, homelessFP);
                fp = homelessFP;
                homelessFP = 0;
            }
        } break;
        case 4:
        {
            u32 fp = fpLong & 0xffffffff;
            u64 altHash = cf_alternative_hash32(cf, hash, fp);

            if (cf_insert_fp32(cf, hash, fp, NULL) || cf_insert_fp32(cf, altHash, fp, NULL)) 
            {
                RedisModule_ReplyWithSimpleString(ctx, "OK");
                return REDISMODULE_OK;
            }

            u32 homelessFP = 0;
            u64 homelessHash = altHash;
            for (int n = 0; n < 500; n++) {
                cf_insert_fp32(cf, homelessHash, fp, &homelessFP);

                if (!homelessFP) {
                    RedisModule_ReplyWithSimpleString(ctx, "OK");
                    return REDISMODULE_OK;
                } 

                homelessHash = cf_alternative_hash32(cf, homelessHash, homelessFP);
                fp = homelessFP;
                homelessFP = 0;
            }
        } break;
        default:
        {

        }
    }
    RedisModule_ReplyWithError(ctx,"ERR too full");
    return REDISMODULE_OK;
}


// CF.REM key slot fp
int CFRem_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    unsigned long long hash;
    u64 fpLong;
    RedisModuleKey *key;
    CuckooFilter *cf;
    COMMAND_4_PREAMBLE(REDISMODULE_READ|REDISMODULE_WRITE)

    bool success = 0;
    switch(cf->fpSize)
    {
        case 1:
            success = cf_delete_fp8(cf, hash, (u8)fpLong);
            break;
        case 2:
            success = cf_delete_fp16(cf, hash, (u16)fpLong);
            break;
        case 4:
            success = cf_delete_fp32(cf, hash, (u32)fpLong);
            break;
    }

    if (!success)
    {
        RedisModule_ReplyWithError(ctx,"ERR tried to delete non-existing item. THE FILTER MIGHT BE COMPROMISED.");
        return REDISMODULE_OK;
    }
                    
    RedisModule_ReplyWithSimpleString(ctx, "OK");
    return REDISMODULE_OK;
}


// CF.CHECK key slot fp
int CFCheck_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    u64 hash;
    u64 fpLong;
    RedisModuleKey *key;
    CuckooFilter *cf;
    COMMAND_4_PREAMBLE(REDISMODULE_READ)

    bool success = 0;
    switch(cf->fpSize)
    {
        case 1:
            success = cf_search_fp8(cf, hash, (u8)fpLong);
            break;
        case 2:
            success = cf_search_fp16(cf, hash, (u16)fpLong);
            break;
        case 4:
            success = cf_search_fp32(cf, hash, (u32)fpLong);
            break;
    }

    if (success){
        RedisModule_ReplyWithLongLong(ctx, 1);
        return REDISMODULE_OK;
    }
    //printf("Failed: \n%s %s \n%llu %llu\n", RedisModule_StringPtrLen(argv[2], NULL), RedisModule_StringPtrLen(argv[3], NULL), hash, fpLong);
    RedisModule_ReplyWithLongLong(ctx, 0);
    return REDISMODULE_OK;
}

// CF.DUMP key
int CFDump_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    RedisModule_AutoMemory(ctx);
    if (argc != 2) return RedisModule_WrongArity(ctx);

    RedisModuleKey *key = RedisModule_OpenKey(ctx, argv[1], REDISMODULE_READ);
    int type = RedisModule_KeyType(key);
    if (type == REDISMODULE_KEYTYPE_EMPTY || RedisModule_ModuleTypeGetType(key) != CuckooFilterType)
    {
        RedisModule_ReplyWithError(ctx, REDISMODULE_ERRORMSG_WRONGTYPE);
        return REDISMODULE_OK;
    }

    CuckooFilter *cf = (CuckooFilter*)RedisModule_ModuleTypeGetValue(key);

    switch(cf->fpSize)
    {
        case 1:
            RedisModule_ReplyWithStringBuffer(ctx, cf->filter, cf->numBuckets * 4);
            break;
        case 2:
            RedisModule_ReplyWithStringBuffer(ctx, cf->filter, cf->numBuckets * 8);
            break;
        case 4:
            RedisModule_ReplyWithStringBuffer(ctx, cf->filter, cf->numBuckets * 8);
            break;
    }

    return REDISMODULE_OK;
}

// CF.UTILS {err, fpsize} err bucklen
int CFUtils_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    RedisModule_AutoMemory(ctx);
    if (argc != 3) return RedisModule_WrongArity(ctx);

    double targetError = strtod(RedisModule_StringPtrLen(argv[1], NULL), NULL);
    u64 buckLen;
    RedisModule_StringToLongLong(argv[2], &buckLen);
    
    RedisModule_ReplyWithDouble(ctx, log2(1/targetError) + log(2*buckLen));

    return REDISMODULE_OK;
}


#ifdef SELFTEST

#define FNV_OFFSET 14695981039346656037ULL
#define FNV_PRIME 1099511628211ULL
#define FNV1A(h, x) (((h) ^ (x)) * FNV_PRIME)
int CFFNV1a_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    RedisModule_AutoMemory(ctx);
    if (argc != 2) return RedisModule_WrongArity(ctx);

    u64 fp;
    RedisModule_StringToLongLong(argv[1], &fp);
    u64 hash = FNV1A(FNV_OFFSET, fp);

    printf("FNV: %llu\n", hash);

    RedisModule_ReplyWithLongLong(ctx, hash);

    return REDISMODULE_OK;
}

#include "../test/src/tests.c"
int CFTest_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    RedisModule_AutoMemory(ctx);
    
    CleanupAllTests(ctx);    
    int errors = RunAllTests(ctx);
    
    if (errors) {
        RedisModule_ReplyWithError(ctx, "ERR test failed");
        return REDISMODULE_OK;
    }
    CleanupAllTests(ctx);    

    RedisModule_ReplyWithSimpleString(ctx, "OK");
    return REDISMODULE_OK;
}
int CFRand_RedisCommand(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    RedisModule_ReplyWithLongLong(ctx, rand());
    return REDISMODULE_OK;
}
#endif


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
    cf->isMulti = RedisModule_LoadUnsigned(rdb);
    cf->fpSize = RedisModule_LoadUnsigned(rdb);
    cf->filter = (u8*)RedisModule_LoadStringBuffer(rdb, &cf->numBuckets);
    cf->numBuckets /= (cf->fpSize == 1 ? 4 : 8);

    return cf;

}

void CFSave (RedisModuleIO *rdb, void *value)
{
    CuckooFilter *cf = value;
    RedisModule_SaveUnsigned(rdb, cf->isMulti);
    RedisModule_SaveUnsigned(rdb, cf->fpSize);
    RedisModule_SaveStringBuffer(rdb, (const char *)cf->filter, 
        cf->numBuckets * (cf->fpSize == 1 ? 4 : 8));
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


/* This function must be present on each Redis module. It is used in order to
 * register the commands into the Redis server. */
int RedisModule_OnLoad(RedisModuleCtx *ctx, RedisModuleString **argv, int argc) {
    if (RedisModule_Init(ctx,"cuckoofilter", 1, REDISMODULE_APIVER_1) == REDISMODULE_ERR) return REDISMODULE_ERR;

    RedisModuleTypeMethods tm = {
        .version = REDISMODULE_TYPE_METHOD_VERSION,
        .rdb_load = CFLoad,
        .rdb_save = CFSave,
        .aof_rewrite = CFRewrite,
        .free = CFFree
    };

    CuckooFilterType = RedisModule_CreateDataType(ctx, "cuckoof-k", CUCKOO_FILTER_ENCODING_VERSION, &tm);
    if (CuckooFilterType == NULL) return REDISMODULE_ERR;

    /* Log the list of parameters passing loading the module. */
    for (int j = 0; j < argc; j++) {
        const char *s = RedisModule_StringPtrLen(argv[j],NULL);
        printf("Module loaded with ARGV[%d] = %s\n", j, s);
    }

    if (RedisModule_CreateCommand(ctx,"cf.init",
        CFInit_RedisCommand,"write",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx,"cf.add",
        CFAdd_RedisCommand,"write",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx,"cf.rem",
        CFRem_RedisCommand,"write",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx,"cf.check",
        CFCheck_RedisCommand,"readonly",1,1,1) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx,"cf.dump",
        CFDump_RedisCommand,"readonly",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

    if (RedisModule_CreateCommand(ctx,"cf.utils",
        CFUtils_RedisCommand,"readonly",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;

#ifdef SELFTEST
    printf("CUCKOO FILTER TEST BUILD -- DO NOT USE IN PRODUCTION\n");
    srand(42);
    if (RedisModule_CreateCommand(ctx,"cf.selftest",
        CFTest_RedisCommand,"readonly",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (RedisModule_CreateCommand(ctx,"cf.rand",
        CFRand_RedisCommand,"readonly",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
    if (RedisModule_CreateCommand(ctx,"cf.fnv",
        CFFNV1a_RedisCommand,"readonly",0,0,0) == REDISMODULE_ERR)
        return REDISMODULE_ERR;
#endif

    return REDISMODULE_OK;
}