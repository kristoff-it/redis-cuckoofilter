redis-cuckoofilter
==================
Hashing-function agnostic Cuckoo filters for Redis based on 
	[zig-cuckoofilter](https://github.com/kristoff-it/zig-cuckoofilter).


What's a Cuckoo Filter?
-----------------------
Cuckoo filters are a probabilistic data structure that allows you to test for 
membership of an element in a set without having to hold the whole set in 
memory.

This is done at the cost of having a probability of getting a false positive 
response, which, in other words, means that they can only answer "Definitely no" 
or "Probably yes". The false positive probability is roughly inversely related 
to how much memory you are willing to allocate to the filter.

The most iconic data structure used for this kind of task are Bloom filters 
but Cuckoo filters boast both better practical performance and efficiency, and, 
more importantly, the ability of **deleting elements from the filter**. 

Bloom filters only support insertion of new items.
Some extensions of Bloom filters have the ability of deleting items but they 
achieve so at the expense of precision or memory usage, resulting in a far worse 
tradeoff compared to what Cuckoo filters offer.


What Makes This Redis Module Interesting
----------------------------------------
Cuckoo filters offer a very interesting division of labour between server and 
clients.

Since Cuckoo filters rely on a single hashing of the original item you want to 
insert, it is possible to off-load that part of the computation to the client. 
In practical terms it means that instead of sending the whole item to Redis, the
clients send `hash` and `fingeprint` of the original item.

### What are the advantages of doing so?
	
- You need to push trough the cable a constant amount of data per item instead 
  of N bytes *(Redis is a remote service afterall, you're going through a UNIX 
  socket at the very least)*.
- To perform well, Cuckoo filters rely on a good choice of fingerprint for each 
  item and it should not be left to the library.
- **The hash function can be decided by you, meaning that this module is 
  hashing-function agnostic**.

The last point is the most important one. 
It allows you to be more flexible in case you need to reason about item hashes 
across different clients potentially written in different languages. 

Additionally, different hashing function families specialize on different use 
cases that might interest you or not. For example some work best for small data 
(< 7 bytes), some the opposite. Some focus more on performance at the expense of 
more collisions, while some others behave better than the rest on peculiar 
platforms.

[This blogpost](http://aras-p.info/blog/2016/08/09/More-Hash-Function-Tests/) 
shows a few benchmarks of different hashing function families.

Considering all of that, the choice of hashing and fingerprinting functions has 
to be up to you.

*For the internal partial hashing that has to happen when reallocating a 
fingerprint server-side, this implementation uses FNV1a which is robust and fast 
for 1 byte inputs (the size of a fingerprint).*

*Thanks to how Cuckoo filters work, that choice is completely transparent to the 
clients.*

Installation 
------------

1. Download a precompiled binary from the 
   [Release section](https://github.com/kristoff-it/redis-cuckoofilter/releases/) 
   of this repo or compile it yourself (instructions at the end of this README).

2. Put `libredis-cuckoofilter.so` module in a folder readable by your Redis 
   server.

3. To try out the module you can send 
   `MODULE LOAD /path/to/libredis-cuckoofilter.so` using redis-cli or a client of 
   your choice.

4. Once you save on disk a key containing a Cuckoo filter you will need to add 
   `loadmodule /path/to/libredis-cuckoofilter.so` to your `redis.conf`, otherwise 
   Redis will not load complaining that it doesn't know how to read some data 
   from the `.rdb` file.


Quickstart
----------

```
redis-cli> MODULE LOAD /path/to/libredis-cuckoofilter.so
OK

redis-cli> CF.INIT test 64K
OK 
 
redis-cli> CF.ADD test 5366164415461427448 97
OK

redis-cli> CF.CHECK test 5366164415461427448 97
(integer) 1

redis-cli> CF.REM test 5366164415461427448 97
OK 

redis-cli> CF.CHECK test 5366164415461427448 97
(integer) 0
```

Client-side quickstart
----------------------
```python
import redis

r = redis.Redis()

# Load the module if you haven't done so already
r.execute_command("module", "load", "/path/to/libredis-cuckoofilter.so")

# Create a filter
r.execute_command("cf.init", "test", "64k")

# Define a fingerprinting function, for hashing we'll use python's builtin `hash()` 
def fingerprint(x):
  return ord(x[0]) # takes the first byte and returns its numerical value

item = "banana"

# Add an item to the filter
r.execute_command("cf.add", "test", hash(item), fingerprint(item))

# Check for its presence
r.execute_command("cf.check", "test", hash(item), finterprint(item)) # => true

# Check for a non-existing item
r.execute_command("cf.check", "test", hash("apple"), fingerprint("apple")) # => false
```

Fingerprint size and error rates
--------------------------------
In Cuckoo filters the number of bytes that we decide to use as fingerprint
will directly impact the maximum false positive error rate of a given filter.
This implementation supports 1, 2 and 4-byte wide fingerprints.

### 1 (3% error)
Error % -> `3.125e-02 (~0.03, i.e. 3%)`

### 2 (0.01% error)
Error % -> `1.22070312e-04 (~0.0001, i.e. 0.01%))`

### 4 (0.0000001% error)
Error % -> `9.31322574e-10 (~0.000000001, i.e. 0.0000001%)`


Complete command list
---------------------

### - `CF.SIZEFOR universe [fpsize] [EXACT]`
#### Complexity: O(1)
#### Example: `CF.SIZEFOR 1000 2 EXACT`
Returns the correct size for a filter that must hold at most `universe` items.
Default `fpsize` is 1, specify a different value if you need an error rate lower
than 3%.
Cuckoo filters should never be filled over 80% of their maximum theoretical capacity
both for performance reasons and because a filter that approaces 100% fill rate will
start refusing inserts with a `ERR too full` error.
This command will automatically pad `universe` for you. Use `EXACT` if you don't want 
that behavior.

### - `CF.CAPACITY key`
#### Complexity: O(1)
#### Example: `CF.CAPACITY mykey`
Returns the theoretical maximum number of items that can be added to the filter present
at `key`. Does not include any padding.

### - `CF.INIT key size [fpsize]`
#### Complexity: O(size)
#### Example: `CF.INIT mykey 64K`
Instantiates a new filter. Use `CF.SIZEFOR` to know the correct value for `size`.
Supported sizes are a power of 2 in this range: `1K .. 8G`.
Default error rate is 3%, use `fpsize` to specify a different target error rate.

### - `CF.ADD key hash fp`
#### Complexity: O(1) 
#### Example `CF.ADD mykey 100 97`
Adds a new item to the filter. Both `hash` and `fp` must be numbers.
In particular, `hash` has to be a 64bit representable number, while `fp`
should be a `fpsize` representable number. As an example, a filter with 
`fpsize` set to `1` will cause the maximum recommended value of `fp` to be `255`.
The `fp` argument is a `u32` so `(2^32)-1` is its maximum valid value, but when
`fpsize` is lower than `4`, high bits will be truncated (e.g. `-1 == 255` when 
`fpsize == 1`).

You can use both signed and unsigned values as long as you are consistent
in their use. Internally all values will be transalted to unsigned.
If a filter is undersized/overfilled or you are adding multiple copies of 
the same item or, worse, you're not properely handling information entropy, 
this command will return `ERR too full`.
Read the extented example in 
  [kristoff-it/zig-cuckoofilter](https://github.com/kristoff-it/zig-cuckoofilter) 
to learn more about misusage scenarios.

### - `CF.REM key hash fp`
#### Complexity: O(1)
#### Example `CF.REM mykey 100 97`
Deletes an item. Accepts the same arguments as `CF.ADD`. 
WARNING: this command must be used to only delete items that were
previously inserted. Trying to delete non-existing items will corrupt the 
filter and cause it to lockdown. When that happens all command will start
returning `ERR broken`, because at that point it will be impossible to 
know what the correct state would be. Incurring in `ERR broken` is 
a usage error and should never happen. Read the extented example in 
  [kristoff-it/zig-cuckoofilter](https://github.com/kristoff-it/zig-cuckoofilter) 
to learn more about misusage scenarios.

### - `CF.CHECK key hash fp`
#### Complexity: O(1)
#### Example `CF.CHECK mykey 100 97`
Checks if an item is present in the filter or not. Returns `1` for the 
positive case and `0` otherwise. Accepts the same arguments as `CF.ADD`.

### - `CF.COUNT key`
#### Complexity: O(1)
#### Example: `CF.COUNT mykey`
Returns the number of items present in the filter.

### - `CF.ISBROKEN key`
#### Complexity: O(1)
#### Example: `CF.ISBROKEN mykey`
Returns `1` if the filter was broken because of misusage of `CF.REM`,
returns `0` otherwise. A broken filter cannot be fixed and will start
returning `ERR broken` from most comamnds.


### - `CF.ISTOOFULL key`
#### Complexity: O(1)
#### Example: `CF.ISTOOFULL mykey`
Returns `1` if the filter is too full, returns `0` otherwise.
This command can return `1` even if you never received a 
`ERR too full` from a call to `CF.ADD`. 
Read the extented example in 
  [kristoff-it/zig-cuckoofilter](https://github.com/kristoff-it/zig-cuckoofilter) 
to learn more about misusage scenarios.

### - `CF.FIXTOOFULL key`
#### Complexity: O(1) big constant
#### Example: `CF.FIXTOOFULL mykey`
If you are adding and also **deleting** items from the filter
but in a moment of *congestion* you ended up ovferfilling the filter,
this command can help re-distribute some items to fix the situation.
It's not a command you should ever rely on because it should never 
be needed if you properly sized your filter using `CF.SIZEFOR`.
Read the extented example in 
  [kristoff-it/zig-cuckoofilter](https://github.com/kristoff-it/zig-cuckoofilter) 
to learn more about misusage scenarios.

Advanced usage
--------------
Checkout 
  [kristoff-it/zig-cuckoofilter](https://github.com/kristoff-it/zig-cuckoofilter) 
for more information about advanced usage of Cuckoo filters and 
how to deal (and most importantly, prevent) failure scenarios.

Planned Features
----------------

- Advanced client-side syncrhonization
    Given that now the logic is bundled in zig-cuckoofilter and that
    it can now be used by any C ABI compatible target (checkout the 
    repo for examples in C, JS, Python and Go), combined with Streams
    it would be possible to keep a client-side Cuckoo filter synced
    with one in Redis, allowing clients to keep reads locally and 
    asyncrhonously sync with Redis to obtain new updates to the filter.

Compiling 
---------
Download the latest Zig compiler version from http://ziglang.org.

### To compile for your native platform
```sh
$ zig build-lib -dynamic -isystem src --release-fast src/redis-cuckoofilter.zig
```

### To cross-compile
```sh
$ zig build-lib -dynamic -isystem src --release-fast -target x86_64-linux --library c src/redis-cuckoofilter.zig
```
Use `zig targets` for the complete list of available targets.

License
-------

MIT License

Copyright (c) 2019 Loris Cro

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
