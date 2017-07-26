redis-cuckoofilter
==================
Hashing-function agnostic Cuckoo filters for Redis


What's a Cuckoo Filter?
-----------------------
Cuckoo filters are a probabilistic data structure that allows you to test for membership
of an element in a set without having to hold the whole set in memory.

This is done at the expense of having a probability of getting a false positive 
response, which, in other words, means that they can only answer "Definitely no" or "Probably yes".
The false positive probability is roughly inversely related to how much memory you are willing to allocate
to the filter.

The most iconic data structure used for this kind of task are Bloom filters 
but Cuckoo filters boast both better practical performance and efficiency, and, more importantly,
the ability of **deleting elements from the filter**. 

Bloom filters only support insertion of new items.
Some extensions of Bloom filters have the ability of deleting items but they achieve
so at the expense of precision or memory usage, resulting in a far worse tradeoff compared to 
what Cuckoo filters offer.

For more information consult:

["Cuckoo Filter: Practically Better Than Bloom"](http://www.cs.cmu.edu/~binfan/papers/conext14_cuckoofilter.pdf) in proceedings of ACM CoNEXT 2014 by Bin Fan, Dave Andersen and Michael Kaminsky


What Makes This Redis Module Interesting
----------------------------------------
Cuckoo filters offer a very interesting division of labour between server and clients.

Since Cuckoo filters rely on a single hashing of the original item you want to insert,
it is possible to off-load that part of the computation to the client. 
In practical terms it means that instead of sending the whole item to Redis, the clients
send {hash, fingerprint} of the original item.

### What are the advantages of doing so?
	
- You need to push trough the cable a constant amount of data per item instead of N bytes *(Redis is a remote service afterall, you're going through a UNIX socket at the very least)*.
- To perform well, Cuckoo filters rely on a good choice of fingerprint for each item and its choice should not be left to the library.
- **The hash function can be decided by you, meaning that this module is hashing-function agnostic**.

The last point is the most important one. It allows you to be more flexible in case you need to reason about item hashes across different clients potentially written in different languages. It's surprisingly difficult to find implementations of filters who's state can be shared between different languages and the first hurdle is that they use different(ly seeded) hashing functions. 

Additionally, different hashing function families specialize on different use cases that might interest you or not. For example some work best for small data (< 7 bytes), some the opposite. Some focus more on performance at the expense of more collisions, while some others behave better than the rest on peculiar platforms.

Considering all of that, the choice of {hashing func, fingerprinting func} has to be up to you.

*In case anyone's wandering, for the internal partial hashing that has to happen when reallocating a fingerprint server-side, I chose FNV1a which is extremely fast for 1 byte inputs (the size of a fingerprint) and thanks to how Cuckoo filters work, that choice is completely transparent to the clients.*



Installation 
------------

1. Download a precompiled binary from the Release tab (or [click here](https://github.com/kristoff-it/redis-cuckoofilter/releases/download/0.1/redis-cuckoofilter-releases.zip)) of this repo or compile with `make all` (linux and osx supported)

2. Put the `redis-cuckoofilter.so` module in a folder readable by your Redis installation

3. To try out the module you can send `MODULE LOAD /path/to/redis-cuckoofilter.so` using redis-cli or a client of your choice

4. Once you save on disk a key containing a Cuckoo filter you will need to add `loadmodule /path/to/redis-cuckoofilter.so` to your `redis.conf`, otherwise Redis will not load complaining that it doesn't know how to read some data from the `.rdb` file.


Usage Example
-------------

```
redis-cli> CF.INIT test 64K 2
OK
 
redis-cli> CF.ADD test 2384788 97
OK

redis-cli> CF.CHECK test 2384788 97
(integer) 1

redis-cli> CF.REM test 2384788 97
OK 

redis-cli> CF.CHECK test 2384788 97
(integer) 0
```

Current API
----------

 (still work in progress)

#### Create a new Cuckoo filter:
`CF.INIT key size bucketsize`

Example: `CF.INIT test 64K 2`

- `size`: string representing the size of the filter in bytes; one of {`1K`, `2K`, `4K`, `8K`, `16K`, `32K`, `64K`, `128K`, `256K`, `512K`, `1M`, `2M`, `4M`, `8M`, `16M`, `32M`, `64M`, `128M`, `256M`, `512M`, `1G`, `2G`, `4G`, `8G`}
- `bucketsize`: number of fingerprints per bucket, one of {2, 4}


Bucketsize of 2 offers sligltly better performance and lower error-rate, 4 offers better fill-rate (i.e. lower chance of encountering the "too full" error). Either choice will not change the size of the filter, it's just a matter of how the space will be used. 
		
In general, it's a good idea to size the filter so that it doesn't get too full, as it both lowers the chance of overcrowding some buckets and prevents the insertion time from growing too much (cf. the linked paper for more information). That said, 2-bucketsize filter can be filled up to ~95% and 4-bucketsize filters can be filled up to ~98% (cf. the linked paper).
	
Replies: `OK`

#### Add an item:
`CF.ADD key hash fp`

Example: `CF.ADD test -8965164415461427448 205`

- `hash`: digest of the original item using a hashing function of your choice encoded as an int64 (*signed* long long).
- `fp`: fingerprint of the original item: 1 byte encoded as 0 - 255

On success replies: `OK`


#### Remove an item:
`CF.REM key hash fp`

Please keep in mind that you are supposed to call this function only on items that have been inserted in the filter. Trying to delete an item that has never been inserted has a small chance of breaking your filter.
	
Returns an error if the item you're trying to remove doesn't exist.

#### Check if an item is part of the filter:
`CF.CHECK key hash fp`

On success replies: `1` if the item seems to be present else `0`

#### Obtain the whole filter:
`CF.DUMP key`

On success replies with the raw bytes of the filter.


Planned Features
----------------

- Cuckoo filters for multisets: currently you can add a maximum of `2 * bucketsize` copies of the same element before getting a `too full` error. Making a filter that adds a counter for each bucketslot would create a filter specifically designed for handling multisets. 

- Easy interface: just plain old `cf.easyadd key item`, `cf.easycheck key item`, for when you are only prototyping a solution.

- Dynamic Cuckoo filters: resize instead of failing with `too full`

