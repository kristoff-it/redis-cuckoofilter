const std = @import("std");
const mem = std.mem;
const cuckoo = @import("./lib/zig-cuckoofilter.zig");
const redis = @import("./redismodule.zig");

pub const CUCKOO_FILTER_ENCODING_VERSION = 2;
pub var Type8: ?*redis.RedisModuleType = null;
pub var Type16: ?*redis.RedisModuleType = null;
pub var Type32: ?*redis.RedisModuleType = null;

// `s` is the prng state. It's persisted for each key
// in order to provide fully deterministic behavior for
// insertions.
pub const Filter8 = struct {
    s: [2]u64,
    cf: cuckoo.Filter8,
};

pub const Filter16 = struct {
    s: [2]u64,
    cf: cuckoo.Filter16,
};

pub const Filter32 = struct {
    s: [2]u64,
    cf: cuckoo.Filter32,
};

pub fn RegisterTypes(ctx: *redis.RedisModuleCtx) !void {

    // 8 bit fingerprint
    Type8 = redis.RedisModule_CreateDataType.?(ctx, "ccf-kff-1", CUCKOO_FILTER_ENCODING_VERSION, &redis.RedisModuleTypeMethods{
        .version = redis.REDISMODULE_TYPE_METHOD_VERSION,
        .rdb_load = CFLoad8,
        .rdb_save = CFSave8,
        .aof_rewrite = CFRewrite,
        .free = CFFree8,
        .mem_usage = CFMemUsage8,
        .digest = CFDigest,
    });
    if (Type8 == null) return error.RegisterError;

    // 16 bit fingerprint
    Type16 = redis.RedisModule_CreateDataType.?(ctx, "ccf-kff-2", CUCKOO_FILTER_ENCODING_VERSION, &redis.RedisModuleTypeMethods{
        .version = redis.REDISMODULE_TYPE_METHOD_VERSION,
        .rdb_load = CFLoad16,
        .rdb_save = CFSave16,
        .aof_rewrite = CFRewrite,
        .free = CFFree16,
        .mem_usage = CFMemUsage16,
        .digest = CFDigest,
    });
    if (Type16 == null) return error.RegisterError;

    // 32 bit fingerprint
    Type32 = redis.RedisModule_CreateDataType.?(ctx, "ccf-kff-4", CUCKOO_FILTER_ENCODING_VERSION, &redis.RedisModuleTypeMethods{
        .version = redis.REDISMODULE_TYPE_METHOD_VERSION,
        .rdb_load = CFLoad32,
        .rdb_save = CFSave32,
        .aof_rewrite = CFRewrite,
        .free = CFFree32,
        .mem_usage = CFMemUsage32,
        .digest = CFDigest,
    });
    if (Type32 == null) return error.RegisterError;
}

export fn CFLoad8(rdb: ?*redis.RedisModuleIO, encver: c_int) ?*c_void {
    return CFLoadImpl(Filter8, rdb, encver);
}
export fn CFLoad16(rdb: ?*redis.RedisModuleIO, encver: c_int) ?*c_void {
    return CFLoadImpl(Filter16, rdb, encver);
}
export fn CFLoad32(rdb: ?*redis.RedisModuleIO, encver: c_int) ?*c_void {
    return CFLoadImpl(Filter32, rdb, encver);
}
inline fn CFLoadImpl(comptime CFType: type, rdb: ?*redis.RedisModuleIO, encver: c_int) ?*c_void {
    if (encver != CUCKOO_FILTER_ENCODING_VERSION) {
        // We should actually log an error here, or try to implement
        // the ability to load older versions of our data structure.
        return null;
    }

    // Allocate cf struct
    var cf = @ptrCast(*CFType, @alignCast(@alignOf(usize), redis.RedisModule_Alloc.?(@sizeOf(CFType))));

    // Load
    const realCFType = @TypeOf(cf.cf);
    cf.* = CFType{
        .s = [2]u64{ redis.RedisModule_LoadUnsigned.?(rdb), redis.RedisModule_LoadUnsigned.?(rdb) },
        .cf = realCFType{
            .rand_fn = null,
            .homeless_fp = @intCast(realCFType.FPType, redis.RedisModule_LoadUnsigned.?(rdb)),
            .homeless_bucket_idx = @intCast(usize, redis.RedisModule_LoadUnsigned.?(rdb)),
            .fpcount = redis.RedisModule_LoadUnsigned.?(rdb),
            .broken = redis.RedisModule_LoadUnsigned.?(rdb) != 0,
            .buckets = blk: {
                var bytes_len: usize = undefined;
                const buckets_ptr = @alignCast(@alignOf(usize), redis.RedisModule_LoadStringBuffer.?(rdb, &bytes_len))[0..bytes_len];
                break :blk realCFType.bytesToBuckets(buckets_ptr) catch @panic("trying to load corrupted buckets from RDB!");
            },
        },
    };

    return cf;
}

export fn CFSave8(rdb: ?*redis.RedisModuleIO, value: ?*c_void) void {
    CFSaveImpl(Filter8, rdb, value);
}
export fn CFSave16(rdb: ?*redis.RedisModuleIO, value: ?*c_void) void {
    CFSaveImpl(Filter16, rdb, value);
}
export fn CFSave32(rdb: ?*redis.RedisModuleIO, value: ?*c_void) void {
    CFSaveImpl(Filter32, rdb, value);
}
inline fn CFSaveImpl(comptime CFType: type, rdb: ?*redis.RedisModuleIO, value: ?*c_void) void {
    const cf = @ptrCast(*CFType, @alignCast(@alignOf(usize), value));

    // Write cuckoo struct data
    redis.RedisModule_SaveUnsigned.?(rdb, cf.s[0]);
    redis.RedisModule_SaveUnsigned.?(rdb, cf.s[1]);
    redis.RedisModule_SaveUnsigned.?(rdb, cf.cf.homeless_fp);
    redis.RedisModule_SaveUnsigned.?(rdb, cf.cf.homeless_bucket_idx);
    redis.RedisModule_SaveUnsigned.?(rdb, cf.cf.fpcount);
    if (cf.cf.broken) redis.RedisModule_SaveUnsigned.?(rdb, 1) else redis.RedisModule_SaveUnsigned.?(rdb, 0);

    // Write buckets
    const bytes = mem.sliceAsBytes(cf.cf.buckets);
    redis.RedisModule_SaveStringBuffer.?(rdb, bytes.ptr, bytes.len);
}

export fn CFFree8(cf: ?*c_void) void {
    CFFreeImpl(Filter8, cf);
}
export fn CFFree16(cf: ?*c_void) void {
    CFFreeImpl(Filter16, cf);
}
export fn CFFree32(cf: ?*c_void) void {
    CFFreeImpl(Filter32, cf);
}
inline fn CFFreeImpl(comptime CFType: type, value: ?*c_void) void {
    const cf = @ptrCast(*CFType, @alignCast(@alignOf(usize), value));
    redis.RedisModule_Free.?(cf.cf.buckets.ptr);
    redis.RedisModule_Free.?(cf);
}

export fn CFMemUsage8(value: ?*const c_void) usize {
    return CFMemUsageImpl(Filter8, value);
}
export fn CFMemUsage16(value: ?*const c_void) usize {
    return CFMemUsageImpl(Filter16, value);
}
export fn CFMemUsage32(value: ?*const c_void) usize {
    return CFMemUsageImpl(Filter32, value);
}
inline fn CFMemUsageImpl(comptime CFType: type, value: ?*const c_void) usize {
    const cf = @ptrCast(*const CFType, @alignCast(@alignOf(usize), value));
    const realCFType = @TypeOf(cf.cf);
    return @sizeOf(CFType) + (mem.sliceAsBytes(cf.cf.buckets).len * @sizeOf(realCFType.FPType));
}

export fn CFRewrite(aof: ?*redis.RedisModuleIO, key: ?*redis.RedisModuleString, value: ?*c_void) void {}

export fn CFDigest(digest: ?*redis.RedisModuleDigest, value: ?*c_void) void {}
