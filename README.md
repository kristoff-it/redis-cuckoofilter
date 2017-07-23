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
	
- Less work for Redis means it can support more clients with the same resources.
- You need to push trough the cable a constant amount of data per item instead of N bytes.
- **The hash function can be decided by you, meaning that this module is hashing-function agnostic**.

The last point is the most important one. It allows you to both be more flexible on the client-side and lets you choose the {hashing func, fingerprinting func} couple that best suits your needs. 



Current API
----------

 (still work in progress)

#### Create a new Cuckoo filter:
`CF.INIT key size bucketsize`

- `size`: string representing the size of the filter in bytes; one of {64K, 128K, 256K, 512K, 1M, 2M, 4M, 8M, 16M, 32M, 64M, 128M, 256M, 512M, 1G, 2G, 4G, 8G}
- `bucketsize`: number of fingerprints per bucket, one of {2, 4}


Bucketsize of 2 offers sligltly better performance and lower error-rate, 4 offers better fill-rate (i.e. lower chance of encountering the "too full" error). Either choice will not change the size of the filter, it's just a matter of how the space will be used. 
		
In general it's a good idea to size the filter so that it never gets fuller than ~60% as it both lowers the chance of overcrowding some buckets and prevents the insertion time from growing too much (cf. the linked paper for more information). That said, 2-bucketsize filter can be filled up to ~95% and 4-bucketsize filters can be filled up to ~98% (cf. the linked paper).
	
Example: `CF.INIT test 64K 2`

On success replies: `OK`

#### Add an item:
`CF.ADD key hash fp`
- `hash`: int64 digest of the original item using a hashing function of your choice. Can also be unsigned.
- `fp`: fingerprint of the original item: 1 byte encoded as 0 - 255

Example: `CF.ADD test -8965164415461427448 205`

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


Installation 
------------

1. Download a precompiled binary from the Release tab (or [click here](https://github.com/kristoff-it/redis-cuckoofilter/releases/download/0.1/redis-cuckoofilter-releases.zip)) of this repo or compile with `make all` (linux and osx supported)

2. Put the `redis-cuckoofilter.so` module in a folder readable by your Redis installation

3. To try out the module you can send `MODULE LOAD /path/to/redis-cuckoofilter.so` using redis-cli or a client of your choice

4. Once you save on disk a key containing a Cuckoo filter you will need to add `loadmodule /path/to/redis-cuckoofilter.so` to your `redis.conf`, otherwise Redis will not load complaining that it doesn't know how to read some data from the `.rdb` file.



Planned Features
----------------

- Cuckoo filters for multisets: currently you can add a maximum of `2 * bucketsize` copies of the same element before getting a `too full` error. Making a filter that adds a counter for each bucketslot would create a filter specifically designed for handling multisets. 

- Easy interface: just plain old `cf.easyadd key item`, `cf.easycheck key item`, for when you are only prototyping a solution.

- Dynamic Cuckoo filters: resize instead of failing with `too full`

