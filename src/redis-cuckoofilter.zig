const builtin = @import("builtin");
const std = @import("std");
const mem = std.mem;
const redis = @import("./redismodule.zig");
const cuckoo = @import("./lib/zig-cuckoofilter.zig");
const t_ccf = @import("./t_cuckoofilter.zig");

// We save the initial state of Xoroshiro seeded at 42 at compile-time,
// used to initialize the prng state for each new cuckoofilter key.
// This way we are able to provide fully deterministic behavior.
const XoroDefaultState: [2]u64 = comptime std.rand.Xoroshiro128.init(42).s;

// Compares two strings ignoring case (ascii strings, not fancy unicode strings).
// Used by commands to check if a given flag (e.g. NX, EXACT, ...) was given as an arugment.
// Specialzied version where one string is comptime known (and all uppercase).
inline fn insensitive_eql(comptime uppercase: []const u8, str: []const u8) bool {
    comptime {
        var i = 0;
        while (i < uppercase.len) : (i += 1) {
            if (uppercase[i] >= 'a' and uppercase[i] <= 'z') {
                @compileError("`insensitive_eql` requires the first argument to be all uppercase");
            }
        }
    }

    if (uppercase.len != str.len) return false;

    if (uppercase.len < 10) {
        comptime var i = 0;
        inline while (i < uppercase.len) : (i += 1) {
            const val = if (str[i] >= 'a' and str[i] <= 'z') str[i] - 32 else str[i];
            if (val != uppercase[i]) return false;
        }
    } else {
        var i = 0;
        while (i < uppercase.len) : (i += 1) {
            const val = if (str[i] >= 'a' and str[i] <= 'z') str[i] - 32 else str[i];
            if (val != uppercase[i]) return false;
        }
    }

    return true;
}

// Given a byte count, returns the equivalent string size (e.g. 1024 -> "1K")
// Specialized version for some powers of 2
fn size2str(size: usize, buf: *[5]u8) ![]u8 {
    var pow_1024: usize = 0;
    var num = size;
    while (num >= 1024) {
        num = try std.math.divExact(usize, num, 1024);
        pow_1024 += 1;
    }

    var letter: u8 = undefined;
    switch (pow_1024) {
        0 => return error.TooSmall,
        1 => letter = 'K',
        2 => letter = 'M',
        3 => letter = 'G',
        else => return error.TooBig,
    }

    // We want to stop at 8G
    if (pow_1024 == 3 and num > 8) return error.TooBig;

    return switch (num) {
        1, 2, 4, 8, 16, 32, 64, 128, 256, 512 => try std.fmt.bufPrint(buf, "{}{c}\x00", num, letter),
        else => error.Error,
    };
}

// Given a size, returns the equivalent byte count (e.g. "1K" -> 1024)
// Specialized version for some powers of 2
fn str2size(str: []const u8) !usize {
    if (str.len < 2 or str.len > 4) return error.Error;

    var pow_1024: usize = undefined;
    switch (str[str.len - 1]) {
        'k', 'K' => pow_1024 = 1024,
        'm', 'M' => pow_1024 = 1024 * 1024,
        'g', 'G' => pow_1024 = 1024 * 1024 * 1024,
        else => return error.Error,
    }

    // Parse the numeric part
    const num: usize = try std.fmt.parseInt(usize, str[0..(str.len - 1)], 10);

    // We want to stop at 8G
    if (pow_1024 == 3 and num > 8) return error.Error;

    return switch (num) {
        1, 2, 4, 8, 16, 32, 64, 128, 256, 512 => try std.math.mul(usize, num, pow_1024),
        else => error.Error,
    };
}

// Parses hash and fp values, used by most commands.
inline fn parse_args(ctx: ?*redis.RedisModuleCtx, argv: [*c]?*redis.RedisModuleString, argc: c_int, hash: *u64, fp: *u32) !void {
    if (argc != 4) {
        _ = redis.RedisModule_WrongArity.?(ctx);
        return error.Error;
    }

    // Parse hash as u64
    var hash_len: usize = undefined;
    var hash_str = redis.RedisModule_StringPtrLen.?(argv[2], &hash_len);
    var hash_start: usize = 0;
    if (hash_len > 0 and hash_str[0] == '-') {
        // Shift forward by 1 to skip the negative sign
        hash_start = 1;
    }

    hash.* = std.fmt.parseInt(u64, hash_str[hash_start..hash_len], 10) catch |err| {
        _ = switch (err) {
            error.Overflow => redis.RedisModule_ReplyWithError.?(ctx, c"ERR hash overflows u64"),
            error.InvalidCharacter => redis.RedisModule_ReplyWithError.?(ctx, c"ERR hash contains bad character"),
        };
        return error.Error;
    };

    // The hash was a negative number
    if (hash_start == 1) {
        hash.* = std.math.sub(u64, std.math.maxInt(u64), hash.*) catch {
            _ = redis.RedisModule_ReplyWithError.?(ctx, c"ERR hash underflows u64");
            return error.Error;
        };
    }

    // Parse fp as u32
    var fp_len: usize = undefined;
    var fp_str = redis.RedisModule_StringPtrLen.?(argv[3], &fp_len);
    var fp_start: usize = 0;
    if (fp_len > 0 and fp_str[0] == '-') {
        // Shift forward by 1 to skip the negative sign
        fp_start = 1;
    }

    fp.* = @bitCast(u32, std.fmt.parseInt(i32, fp_str[fp_start..fp_len], 10) catch |err| {
        _ = switch (err) {
            error.Overflow => redis.RedisModule_ReplyWithError.?(ctx, c"ERR fp overflows u32"),
            error.InvalidCharacter => redis.RedisModule_ReplyWithError.?(ctx, c"ERR fp contains bad character"),
        };
        return error.Error;
    });

    // The fp was a negative number
    if (fp_start == 1) {
        fp.* = std.math.sub(u32, std.math.maxInt(u32), fp.*) catch {
            _ = redis.RedisModule_ReplyWithError.?(ctx, c"ERR fp underflows u32");
            return error.Error;
        };
    }
}

// Registers the module and its commands.
export fn RedisModule_OnLoad(ctx: *redis.RedisModuleCtx, argv: [*c]*redis.RedisModuleString, argc: c_int) c_int {
    if (redis.RedisModule_Init(ctx, c"cuckoofilter", 1, redis.REDISMODULE_APIVER_1) == redis.REDISMODULE_ERR) {
        return redis.REDISMODULE_ERR;
    }

    // Register our custom types
    t_ccf.RegisterTypes(ctx) catch return redis.REDISMODULE_ERR;

    // Register our commands
    registerCommand(ctx, c"cf.init", CF_INIT, c"write deny-oom", 1, 1, 1) catch return redis.REDISMODULE_ERR;
    registerCommand(ctx, c"cf.rem", CF_REM, c"write fast", 1, 1, 1) catch return redis.REDISMODULE_ERR;
    registerCommand(ctx, c"cf.add", CF_ADD, c"write fast", 1, 1, 1) catch return redis.REDISMODULE_ERR;
    registerCommand(ctx, c"cf.fixtoofull", CF_FIXTOOFULL, c"write fast", 1, 1, 1) catch return redis.REDISMODULE_ERR;
    registerCommand(ctx, c"cf.check", CF_CHECK, c"readonly fast", 1, 1, 1) catch return redis.REDISMODULE_ERR;
    registerCommand(ctx, c"cf.count", CF_COUNT, c"readonly fast", 1, 1, 1) catch return redis.REDISMODULE_ERR;
    registerCommand(ctx, c"cf.isbroken", CF_ISBROKEN, c"readonly fast", 1, 1, 1) catch return redis.REDISMODULE_ERR;
    registerCommand(ctx, c"cf.istoofull", CF_ISTOOFULL, c"readonly fast", 1, 1, 1) catch return redis.REDISMODULE_ERR;
    registerCommand(ctx, c"cf.capacity", CF_CAPACITY, c"fast allow-loading allow-stale", 0, 0, 0) catch return redis.REDISMODULE_ERR;
    registerCommand(ctx, c"cf.sizefor", CF_SIZEFOR, c"fast allow-loading allow-stale", 0, 0, 0) catch return redis.REDISMODULE_ERR;

    return redis.REDISMODULE_OK;
}

inline fn registerCommand(ctx: *redis.RedisModuleCtx, cmd: [*c]const u8, func: redis.RedisModuleCmdFunc, mode: [*c]const u8, firstkey: c_int, lastkey: c_int, keystep: c_int) !void {
    const err = redis.RedisModule_CreateCommand.?(ctx, cmd, func, mode, firstkey, lastkey, keystep);
    if (err == redis.REDISMODULE_ERR) return error.Error;
}

// CF.INIT size [fpsize]
export fn CF_INIT(ctx: ?*redis.RedisModuleCtx, argv: [*c]?*redis.RedisModuleString, argc: c_int) c_int {
    if (argc != 3 and argc != 4) return redis.RedisModule_WrongArity.?(ctx);

    // size argument
    var size_len: usize = undefined;
    const size_str = redis.RedisModule_StringPtrLen.?(argv[2], &size_len)[0..size_len];
    const size = str2size(size_str) catch return redis.RedisModule_ReplyWithError.?(ctx, c"ERR bad size");

    // fpsize argument
    var fp_size = "1"[0..];
    if (argc == 4) {
        var fp_size_len: usize = undefined;
        fp_size = redis.RedisModule_StringPtrLen.?(argv[3], &fp_size_len)[0..fp_size_len];
    }

    // Obtain the key from Redis.
    var key = @ptrCast(?*redis.RedisModuleKey, redis.RedisModule_OpenKey.?(ctx, argv[1], redis.REDISMODULE_READ | redis.REDISMODULE_WRITE));
    defer redis.RedisModule_CloseKey.?(key);

    var keyType = redis.RedisModule_KeyType.?(key);
    if (keyType != redis.REDISMODULE_KEYTYPE_EMPTY) return redis.RedisModule_ReplyWithError.?(ctx, c"ERR key already exists");

    // New Cuckoo Filter!
    if (fp_size.len != 1) return redis.RedisModule_ReplyWithError.?(ctx, c"ERR bad fpsize");
    return switch (fp_size[0]) {
        '1' => do_init(t_ccf.Filter8, ctx, key, size),
        '2' => do_init(t_ccf.Filter16, ctx, key, size),
        '4' => do_init(t_ccf.Filter32, ctx, key, size),
        else => return redis.RedisModule_ReplyWithError.?(ctx, c"ERR bad fpsize"),
    };
}

inline fn do_init(comptime CFType: type, ctx: ?*redis.RedisModuleCtx, key: ?*redis.RedisModuleKey, size: usize) c_int {
    var cf = @ptrCast(*CFType, @alignCast(@alignOf(usize), redis.RedisModule_Alloc.?(@sizeOf(CFType))));
    var buckets = @ptrCast([*]u8, @alignCast(@alignOf(usize), redis.RedisModule_Alloc.?(size)));
    const realCFType = @typeOf(cf.cf);

    cf.s = XoroDefaultState;
    cf.cf = realCFType.init(buckets[0..size]) catch return redis.RedisModule_ReplyWithError.?(ctx, c"ERR could not create filter");

    switch (CFType) {
        t_ccf.Filter8 => _ = redis.RedisModule_ModuleTypeSetValue.?(key, t_ccf.Type8, cf),
        t_ccf.Filter16 => _ = redis.RedisModule_ModuleTypeSetValue.?(key, t_ccf.Type16, cf),
        t_ccf.Filter32 => _ = redis.RedisModule_ModuleTypeSetValue.?(key, t_ccf.Type32, cf),
        else => unreachable,
    }

    _ = redis.RedisModule_ReplicateVerbatim.?(ctx);
    return redis.RedisModule_ReplyWithSimpleString.?(ctx, c"OK");
}

// CF.ADD key hash fp
export fn CF_ADD(ctx: ?*redis.RedisModuleCtx, argv: [*c]?*redis.RedisModuleString, argc: c_int) c_int {
    var hash: u64 = undefined;
    var fp: u32 = undefined;
    parse_args(ctx, argv, argc, &hash, &fp) catch return redis.REDISMODULE_OK;

    var key = @ptrCast(?*redis.RedisModuleKey, redis.RedisModule_OpenKey.?(ctx, argv[1], redis.REDISMODULE_READ | redis.REDISMODULE_WRITE));
    defer redis.RedisModule_CloseKey.?(key);

    if (redis.RedisModule_KeyType.?(key) == redis.REDISMODULE_KEYTYPE_EMPTY)
        return redis.RedisModule_ReplyWithError.?(ctx, c"ERR key does not exist");

    const keyType = redis.RedisModule_ModuleTypeGetType.?(key);
    return if (keyType == t_ccf.Type8) do_add(t_ccf.Filter8, ctx, key, hash, fp) else if (keyType == t_ccf.Type16) do_add(t_ccf.Filter16, ctx, key, hash, fp) else if (keyType == t_ccf.Type32) do_add(t_ccf.Filter32, ctx, key, hash, fp) else redis.RedisModule_ReplyWithError.?(ctx, redis.REDISMODULE_ERRORMSG_WRONGTYPE);
}

inline fn do_add(comptime CFType: type, ctx: ?*redis.RedisModuleCtx, key: ?*redis.RedisModuleKey, hash: u64, fp: u32) c_int {
    const cf = @ptrCast(*CFType, @alignCast(@alignOf(usize), redis.RedisModule_ModuleTypeGetValue.?(key)));
    const realCFType = @typeOf(cf.cf);
    cuckoo.set_default_prng_state(cf.s);
    defer {
        cf.s = cuckoo.get_default_prng_state();
    }

    _ = redis.RedisModule_ReplicateVerbatim.?(ctx);
    return if (cf.cf.add(hash, @truncate(realCFType.FPType, fp)))
        redis.RedisModule_ReplyWithSimpleString.?(ctx, c"OK")
    else |err| switch (err) {
        error.Broken => redis.RedisModule_ReplyWithError.?(ctx, c"ERR filter is broken"),
        error.TooFull => redis.RedisModule_ReplyWithError.?(ctx, c"ERR too full"),
    };
}

// CF.CHECK key hash fp
export fn CF_CHECK(ctx: ?*redis.RedisModuleCtx, argv: [*c]?*redis.RedisModuleString, argc: c_int) c_int {
    var hash: u64 = undefined;
    var fp: u32 = undefined;
    parse_args(ctx, argv, argc, &hash, &fp) catch return redis.REDISMODULE_OK;

    var key = @ptrCast(?*redis.RedisModuleKey, redis.RedisModule_OpenKey.?(ctx, argv[1], redis.REDISMODULE_READ | redis.REDISMODULE_WRITE));
    defer redis.RedisModule_CloseKey.?(key);

    if (redis.RedisModule_KeyType.?(key) == redis.REDISMODULE_KEYTYPE_EMPTY)
        return redis.RedisModule_ReplyWithError.?(ctx, c"ERR key does not exist");

    const keyType = redis.RedisModule_ModuleTypeGetType.?(key);
    return if (keyType == t_ccf.Type8) do_check(t_ccf.Filter8, ctx, key, hash, fp) else if (keyType == t_ccf.Type16) do_check(t_ccf.Filter16, ctx, key, hash, fp) else if (keyType == t_ccf.Type32) do_check(t_ccf.Filter32, ctx, key, hash, fp) else redis.RedisModule_ReplyWithError.?(ctx, redis.REDISMODULE_ERRORMSG_WRONGTYPE);
}

inline fn do_check(comptime CFType: type, ctx: ?*redis.RedisModuleCtx, key: ?*redis.RedisModuleKey, hash: u64, fp: u32) c_int {
    const cf = @ptrCast(*CFType, @alignCast(@alignOf(usize), redis.RedisModule_ModuleTypeGetValue.?(key)));
    const realCFType = @typeOf(cf.cf);
    return if (cf.cf.maybe_contains(hash, @truncate(realCFType.FPType, fp))) |maybe_found|
        redis.RedisModule_ReplyWithSimpleString.?(ctx, if (maybe_found) c"1" else c"0")
    else |err| switch (err) {
        error.Broken => redis.RedisModule_ReplyWithError.?(ctx, c"ERR filter is broken"),
    };
}

// CF.REM key hash fp
export fn CF_REM(ctx: ?*redis.RedisModuleCtx, argv: [*c]?*redis.RedisModuleString, argc: c_int) c_int {
    var hash: u64 = undefined;
    var fp: u32 = undefined;
    parse_args(ctx, argv, argc, &hash, &fp) catch return redis.REDISMODULE_OK;

    var key = @ptrCast(?*redis.RedisModuleKey, redis.RedisModule_OpenKey.?(ctx, argv[1], redis.REDISMODULE_READ | redis.REDISMODULE_WRITE));
    defer redis.RedisModule_CloseKey.?(key);

    if (redis.RedisModule_KeyType.?(key) == redis.REDISMODULE_KEYTYPE_EMPTY)
        return redis.RedisModule_ReplyWithError.?(ctx, c"ERR key does not exist");

    const keyType = redis.RedisModule_ModuleTypeGetType.?(key);
    return if (keyType == t_ccf.Type8) do_rem(t_ccf.Filter8, ctx, key, hash, fp) else if (keyType == t_ccf.Type16) do_rem(t_ccf.Filter16, ctx, key, hash, fp) else if (keyType == t_ccf.Type32) do_rem(t_ccf.Filter32, ctx, key, hash, fp) else redis.RedisModule_ReplyWithError.?(ctx, redis.REDISMODULE_ERRORMSG_WRONGTYPE);
}

inline fn do_rem(comptime CFType: type, ctx: ?*redis.RedisModuleCtx, key: ?*redis.RedisModuleKey, hash: u64, fp: u32) c_int {
    const cf = @ptrCast(*CFType, @alignCast(@alignOf(usize), redis.RedisModule_ModuleTypeGetValue.?(key)));
    const realCFType = @typeOf(cf.cf);

    _ = redis.RedisModule_ReplicateVerbatim.?(ctx);
    return if (cf.cf.remove(hash, @truncate(realCFType.FPType, fp)))
        redis.RedisModule_ReplyWithSimpleString.?(ctx, c"OK")
    else |err| switch (err) {
        error.Broken => redis.RedisModule_ReplyWithError.?(ctx, c"ERR filter is broken"),
    };
}

// CF.FIXTOOFULL key
export fn CF_FIXTOOFULL(ctx: ?*redis.RedisModuleCtx, argv: [*c]?*redis.RedisModuleString, argc: c_int) c_int {
    if (argc != 2) return redis.RedisModule_WrongArity.?(ctx);

    var key = @ptrCast(?*redis.RedisModuleKey, redis.RedisModule_OpenKey.?(ctx, argv[1], redis.REDISMODULE_READ | redis.REDISMODULE_WRITE));
    defer redis.RedisModule_CloseKey.?(key);

    if (redis.RedisModule_KeyType.?(key) == redis.REDISMODULE_KEYTYPE_EMPTY)
        return redis.RedisModule_ReplyWithError.?(ctx, c"ERR key does not exist");

    const keyType = redis.RedisModule_ModuleTypeGetType.?(key);
    return if (keyType == t_ccf.Type8) do_fixtoofull(t_ccf.Filter8, ctx, key) else if (keyType == t_ccf.Type16) do_fixtoofull(t_ccf.Filter16, ctx, key) else if (keyType == t_ccf.Type32) do_fixtoofull(t_ccf.Filter32, ctx, key) else redis.RedisModule_ReplyWithError.?(ctx, redis.REDISMODULE_ERRORMSG_WRONGTYPE);
}

inline fn do_fixtoofull(comptime CFType: type, ctx: ?*redis.RedisModuleCtx, key: ?*redis.RedisModuleKey) c_int {
    const cf = @ptrCast(*CFType, @alignCast(@alignOf(usize), redis.RedisModule_ModuleTypeGetValue.?(key)));
    const realCFType = @typeOf(cf.cf);
    cuckoo.set_default_prng_state(cf.s);
    defer {
        cf.s = cuckoo.get_default_prng_state();
    }

    _ = redis.RedisModule_ReplicateVerbatim.?(ctx);
    return if (cf.cf.fix_toofull())
        redis.RedisModule_ReplyWithSimpleString.?(ctx, c"OK")
    else |err| switch (err) {
        error.Broken => redis.RedisModule_ReplyWithError.?(ctx, c"ERR filter is broken"),
        error.TooFull => redis.RedisModule_ReplyWithError.?(ctx, c"ERR too full"),
    };
}

// CF.COUNT key
export fn CF_COUNT(ctx: ?*redis.RedisModuleCtx, argv: [*c]?*redis.RedisModuleString, argc: c_int) c_int {
    if (argc != 2) return redis.RedisModule_WrongArity.?(ctx);

    var key = @ptrCast(?*redis.RedisModuleKey, redis.RedisModule_OpenKey.?(ctx, argv[1], redis.REDISMODULE_READ | redis.REDISMODULE_WRITE));
    defer redis.RedisModule_CloseKey.?(key);

    if (redis.RedisModule_KeyType.?(key) == redis.REDISMODULE_KEYTYPE_EMPTY)
        return redis.RedisModule_ReplyWithError.?(ctx, c"ERR key does not exist");

    const keyType = redis.RedisModule_ModuleTypeGetType.?(key);
    return if (keyType == t_ccf.Type8) do_count(t_ccf.Filter8, ctx, key) else if (keyType == t_ccf.Type16) do_count(t_ccf.Filter16, ctx, key) else if (keyType == t_ccf.Type32) do_count(t_ccf.Filter32, ctx, key) else redis.RedisModule_ReplyWithError.?(ctx, redis.REDISMODULE_ERRORMSG_WRONGTYPE);
}

inline fn do_count(comptime CFType: type, ctx: ?*redis.RedisModuleCtx, key: ?*redis.RedisModuleKey) c_int {
    const cf = @ptrCast(*CFType, @alignCast(@alignOf(usize), redis.RedisModule_ModuleTypeGetValue.?(key)));
    return if (cf.cf.count()) |count|
        redis.RedisModule_ReplyWithLongLong.?(ctx, @intCast(c_longlong, count))
    else |err| switch (err) {
        error.Broken => redis.RedisModule_ReplyWithError.?(ctx, c"ERR filter is broken"),
    };
}

// CF.ISTOOFULL key
export fn CF_ISTOOFULL(ctx: ?*redis.RedisModuleCtx, argv: [*c]?*redis.RedisModuleString, argc: c_int) c_int {
    if (argc != 2) return redis.RedisModule_WrongArity.?(ctx);

    var key = @ptrCast(?*redis.RedisModuleKey, redis.RedisModule_OpenKey.?(ctx, argv[1], redis.REDISMODULE_READ | redis.REDISMODULE_WRITE));
    defer redis.RedisModule_CloseKey.?(key);

    if (redis.RedisModule_KeyType.?(key) == redis.REDISMODULE_KEYTYPE_EMPTY)
        return redis.RedisModule_ReplyWithError.?(ctx, c"ERR key does not exist");

    const keyType = redis.RedisModule_ModuleTypeGetType.?(key);
    return if (keyType == t_ccf.Type8) do_istoofull(t_ccf.Filter8, ctx, key) else if (keyType == t_ccf.Type16) do_istoofull(t_ccf.Filter16, ctx, key) else if (keyType == t_ccf.Type32) do_istoofull(t_ccf.Filter32, ctx, key) else redis.RedisModule_ReplyWithError.?(ctx, redis.REDISMODULE_ERRORMSG_WRONGTYPE);
}

inline fn do_istoofull(comptime CFType: type, ctx: ?*redis.RedisModuleCtx, key: ?*redis.RedisModuleKey) c_int {
    const cf = @ptrCast(*CFType, @alignCast(@alignOf(usize), redis.RedisModule_ModuleTypeGetValue.?(key)));
    return redis.RedisModule_ReplyWithSimpleString.?(ctx, if (cf.cf.is_toofull()) c"1" else c"0");
}

// CF.ISBROKEN key
export fn CF_ISBROKEN(ctx: ?*redis.RedisModuleCtx, argv: [*c]?*redis.RedisModuleString, argc: c_int) c_int {
    if (argc != 2) return redis.RedisModule_WrongArity.?(ctx);

    var key = @ptrCast(?*redis.RedisModuleKey, redis.RedisModule_OpenKey.?(ctx, argv[1], redis.REDISMODULE_READ | redis.REDISMODULE_WRITE));
    defer redis.RedisModule_CloseKey.?(key);

    if (redis.RedisModule_KeyType.?(key) == redis.REDISMODULE_KEYTYPE_EMPTY)
        return redis.RedisModule_ReplyWithError.?(ctx, c"ERR key does not exist");

    const keyType = redis.RedisModule_ModuleTypeGetType.?(key);
    return if (keyType == t_ccf.Type8) do_isbroken(t_ccf.Filter8, ctx, key) else if (keyType == t_ccf.Type16) do_isbroken(t_ccf.Filter16, ctx, key) else if (keyType == t_ccf.Type32) do_isbroken(t_ccf.Filter32, ctx, key) else redis.RedisModule_ReplyWithError.?(ctx, redis.REDISMODULE_ERRORMSG_WRONGTYPE);
}

inline fn do_isbroken(comptime CFType: type, ctx: ?*redis.RedisModuleCtx, key: ?*redis.RedisModuleKey) c_int {
    const cf = @ptrCast(*CFType, @alignCast(@alignOf(usize), redis.RedisModule_ModuleTypeGetValue.?(key)));
    return redis.RedisModule_ReplyWithSimpleString.?(ctx, if (cf.cf.is_broken()) c"1" else c"0");
}

// CF.CAPACITY size [fpsize]
export fn CF_CAPACITY(ctx: ?*redis.RedisModuleCtx, argv: [*c]?*redis.RedisModuleString, argc: c_int) c_int {
    if (argc != 2 and argc != 3) return redis.RedisModule_WrongArity.?(ctx);

    // SIZE argument
    var size_len: usize = undefined;
    const size_str = redis.RedisModule_StringPtrLen.?(argv[1], &size_len)[0..size_len];

    // FPSIZE argument
    var fp_size = "1"[0..];
    if (argc == 3) {
        var fpsize_len: usize = undefined;
        fp_size = redis.RedisModule_StringPtrLen.?(argv[2], &fpsize_len)[0..fpsize_len];
    }

    const size = str2size(size_str) catch return redis.RedisModule_ReplyWithError.?(ctx, c"ERR bad size");

    if (fp_size.len != 1) return redis.RedisModule_ReplyWithError.?(ctx, c"ERR bad fpsize");
    return switch (fp_size[0]) {
        '1' => redis.RedisModule_ReplyWithLongLong.?(ctx, @intCast(c_longlong, cuckoo.Filter8.capacity(size))),
        '2' => redis.RedisModule_ReplyWithLongLong.?(ctx, @intCast(c_longlong, cuckoo.Filter16.capacity(size))),
        '4' => redis.RedisModule_ReplyWithLongLong.?(ctx, @intCast(c_longlong, cuckoo.Filter32.capacity(size))),
        else => redis.RedisModule_ReplyWithError.?(ctx, c"ERR bad fpsize"),
    };
}

// CF.SIZEFOR universe [fpsize] [EXACT]
export fn CF_SIZEFOR(ctx: ?*redis.RedisModuleCtx, argv: [*c]?*redis.RedisModuleString, argc: c_int) c_int {
    if (argc != 2 and argc != 3 and argc != 4) return redis.RedisModule_WrongArity.?(ctx);

    // Parse universe
    var uni_len: usize = undefined;
    var uni_str = redis.RedisModule_StringPtrLen.?(argv[1], &uni_len);
    const universe = std.fmt.parseInt(usize, uni_str[0..uni_len], 10) catch |err| return switch (err) {
        error.Overflow => redis.RedisModule_ReplyWithError.?(ctx, c"ERR universe overflows usize"),
        error.InvalidCharacter => redis.RedisModule_ReplyWithError.?(ctx, c"ERR universe contains bad character"),
    };

    // Parse fpsize and EXACT
    var fp_size = "1"[0..];
    var exact = false;
    if (argc > 2) {
        var arg2len: usize = undefined;
        const arg2 = redis.RedisModule_StringPtrLen.?(argv[2], &arg2len)[0..arg2len];
        if (insensitive_eql("EXACT", arg2)) exact = true else fp_size = arg2;
    }

    if (argc == 4) {
        if (exact) return redis.RedisModule_ReplyWithError.?(ctx, c"ERR bad fpsize");
        var arg3len: usize = undefined;
        const arg3 = redis.RedisModule_StringPtrLen.?(argv[3], &arg3len)[0..arg3len];
        if (insensitive_eql("EXACT", arg3)) exact = true else return redis.RedisModule_WrongArity.?(ctx);
    }

    if (fp_size.len != 1) return redis.RedisModule_ReplyWithError.?(ctx, c"ERR bad fpsize");
    return switch (fp_size[0]) {
        '1' => do_sizefor(cuckoo.Filter8, ctx, universe, exact),
        '2' => do_sizefor(cuckoo.Filter16, ctx, universe, exact),
        '4' => do_sizefor(cuckoo.Filter32, ctx, universe, exact),
        else => redis.RedisModule_ReplyWithError.?(ctx, c"ERR bad fpsize"),
    };
}

fn do_sizefor(comptime CFType: type, ctx: ?*redis.RedisModuleCtx, universe: usize, exact: bool) c_int {
    var buf: [5]u8 = undefined;
    const size = if (exact) CFType.size_for_exactly(universe) else CFType.size_for(universe);
    _ = size2str(std.math.max(1024, size), &buf) catch |err| switch (err) {
        error.TooBig => return redis.RedisModule_ReplyWithError.?(ctx, c"ERR unsupported resulting size (8G max)"),
        else => return redis.RedisModule_ReplyWithError.?(ctx, c"ERR unexpected error. Please report it at github.com/kristoff-it/redis-cuckoofilter"),
    };
    return redis.RedisModule_ReplyWithSimpleString.?(ctx, @ptrCast([*c]const u8, &buf));
}

test "insensitive_eql" {
    std.testing.expect(insensitive_eql("EXACT", "EXACT"));
    std.testing.expect(insensitive_eql("EXACT", "exact"));
    std.testing.expect(insensitive_eql("EXACT", "exacT"));
    std.testing.expect(insensitive_eql("EXACT", "eXacT"));
    std.testing.expect(insensitive_eql("", ""));
    std.testing.expect(insensitive_eql("1", "1"));
    std.testing.expect(insensitive_eql("1234", "1234"));
    std.testing.expect(!insensitive_eql("", "1"));
    std.testing.expect(!insensitive_eql("1", "2"));
    std.testing.expect(!insensitive_eql("1", "12"));
    std.testing.expect(!insensitive_eql("EXACT", "AXACT"));
    std.testing.expect(!insensitive_eql("EXACT", "ESACT"));
    std.testing.expect(!insensitive_eql("EXACT", "EXECT"));
    std.testing.expect(!insensitive_eql("EXACT", "EXAZT"));
    std.testing.expect(!insensitive_eql("EXACT", "EXACZ"));
}

test "size2str" {
    var buf: [5]u8 = undefined;
    std.testing.expect(mem.eql(u8, "1K\x00", size2str(1024 * 1, &buf) catch unreachable));
    std.testing.expect(mem.eql(u8, "2K\x00", size2str(1024 * 2, &buf) catch unreachable));
    std.testing.expect(mem.eql(u8, "4K\x00", size2str(1024 * 4, &buf) catch unreachable));
    std.testing.expect(mem.eql(u8, "8K\x00", size2str(1024 * 8, &buf) catch unreachable));
    std.testing.expect(mem.eql(u8, "16K\x00", size2str(1024 * 16, &buf) catch unreachable));
    std.testing.expect(mem.eql(u8, "32K\x00", size2str(1024 * 32, &buf) catch unreachable));
    std.testing.expect(mem.eql(u8, "64K\x00", size2str(1024 * 64, &buf) catch unreachable));
    std.testing.expect(mem.eql(u8, "128K\x00", size2str(1024 * 128, &buf) catch unreachable));
    std.testing.expect(mem.eql(u8, "256K\x00", size2str(1024 * 256, &buf) catch unreachable));
    std.testing.expect(mem.eql(u8, "512K\x00", size2str(1024 * 512, &buf) catch unreachable));
    std.testing.expect(mem.eql(u8, "1M\x00", size2str(1024 * 1024 * 1, &buf) catch unreachable));
    std.testing.expect(mem.eql(u8, "2M\x00", size2str(1024 * 1024 * 2, &buf) catch unreachable));
    std.testing.expect(mem.eql(u8, "4M\x00", size2str(1024 * 1024 * 4, &buf) catch unreachable));
    std.testing.expect(mem.eql(u8, "8M\x00", size2str(1024 * 1024 * 8, &buf) catch unreachable));
    std.testing.expect(mem.eql(u8, "16M\x00", size2str(1024 * 1024 * 16, &buf) catch unreachable));
    std.testing.expect(mem.eql(u8, "32M\x00", size2str(1024 * 1024 * 32, &buf) catch unreachable));
    std.testing.expect(mem.eql(u8, "64M\x00", size2str(1024 * 1024 * 64, &buf) catch unreachable));
    std.testing.expect(mem.eql(u8, "128M\x00", size2str(1024 * 1024 * 128, &buf) catch unreachable));
    std.testing.expect(mem.eql(u8, "256M\x00", size2str(1024 * 1024 * 256, &buf) catch unreachable));
    std.testing.expect(mem.eql(u8, "512M\x00", size2str(1024 * 1024 * 512, &buf) catch unreachable));
    std.testing.expect(mem.eql(u8, "1G\x00", size2str(1024 * 1024 * 1024 * 1, &buf) catch unreachable));
    std.testing.expect(mem.eql(u8, "2G\x00", size2str(1024 * 1024 * 1024 * 2, &buf) catch unreachable));
    std.testing.expect(mem.eql(u8, "4G\x00", size2str(1024 * 1024 * 1024 * 4, &buf) catch unreachable));
    std.testing.expect(mem.eql(u8, "8G\x00", size2str(1024 * 1024 * 1024 * 8, &buf) catch unreachable));
    std.testing.expectError(error.TooSmall, size2str(1, &buf));
    std.testing.expectError(error.UnexpectedRemainder, size2str(1025, &buf));
    std.testing.expectError(error.TooSmall, size2str(0, &buf));
}

test "str2size" {
    std.testing.expect(1024 * 1 == str2size("1K") catch unreachable);
    std.testing.expect(1024 * 2 == str2size("2K") catch unreachable);
    std.testing.expect(1024 * 4 == str2size("4K") catch unreachable);
    std.testing.expect(1024 * 8 == str2size("8K") catch unreachable);
    std.testing.expect(1024 * 16 == str2size("16K") catch unreachable);
    std.testing.expect(1024 * 32 == str2size("32K") catch unreachable);
    std.testing.expect(1024 * 64 == str2size("64K") catch unreachable);
    std.testing.expect(1024 * 128 == str2size("128K") catch unreachable);
    std.testing.expect(1024 * 256 == str2size("256K") catch unreachable);
    std.testing.expect(1024 * 512 == str2size("512K") catch unreachable);
    std.testing.expect(1024 * 1024 * 1 == str2size("1M") catch unreachable);
    std.testing.expect(1024 * 1024 * 2 == str2size("2M") catch unreachable);
    std.testing.expect(1024 * 1024 * 4 == str2size("4M") catch unreachable);
    std.testing.expect(1024 * 1024 * 8 == str2size("8M") catch unreachable);
    std.testing.expect(1024 * 1024 * 16 == str2size("16M") catch unreachable);
    std.testing.expect(1024 * 1024 * 32 == str2size("32M") catch unreachable);
    std.testing.expect(1024 * 1024 * 64 == str2size("64M") catch unreachable);
    std.testing.expect(1024 * 1024 * 128 == str2size("128M") catch unreachable);
    std.testing.expect(1024 * 1024 * 256 == str2size("256M") catch unreachable);
    std.testing.expect(1024 * 1024 * 512 == str2size("512M") catch unreachable);
    std.testing.expect(1024 * 1024 * 1024 * 1 == str2size("1G") catch unreachable);
    std.testing.expect(1024 * 1024 * 1024 * 2 == str2size("2G") catch unreachable);
    std.testing.expect(1024 * 1024 * 1024 * 4 == str2size("4G") catch unreachable);
    std.testing.expect(1024 * 1024 * 1024 * 8 == str2size("8G") catch unreachable);
    std.testing.expectError(error.Error, str2size(""));
    std.testing.expectError(error.Error, str2size("5K"));
    std.testing.expectError(error.Error, str2size("55"));
    std.testing.expectError(error.Error, str2size("800G"));
}
