redis-cuckoofilter
==================
Hashing-function agnostic Cuckoo filters for Redis


What's a Cuckoo Filter?
-----------------------
Cuckoo filters are a probabilistic data structure that allows you to test for 
membership of an element in a set without having to hold the whole set in 
memory.

This is done at the expense of having a probability of getting a false positive 
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

For more information consult:

["Cuckoo Filter: Practically Better Than Bloom"](http://www.cs.cmu.edu/~binfan/papers/conext14_cuckoofilter.pdf) 
in proceedings of ACM CoNEXT 2014 by Bin Fan, Dave Andersen and Michael Kaminsky


What Makes This Redis Module Interesting
----------------------------------------
Cuckoo filters offer a very interesting division of labour between server and 
clients.

Since Cuckoo filters rely on a single hashing of the original item you want to 
insert, it is possible to off-load that part of the computation to the client. 
In practical terms it means that instead of sending the whole item to Redis, the
clients send {hash, fingerprint} of the original item.

### What are the advantages of doing so?
	
- You need to push trough the cable a constant amount of data per item instead of N bytes *(Redis is a remote service afterall, you're going through a UNIX socket at the very least)*.
- To perform well, Cuckoo filters rely on a good choice of fingerprint for each item and it should not be left to the library.
- **The hash function can be decided by you, meaning that this module is hashing-function agnostic**.

The last point is the most important one. 
It allows you to be more flexible in case you need to reason about item hashes 
across different clients potentially written in different languages. 

Additionally, different hashing function families specialize on different use 
cases that might interest you or not. For example some work best for small data 
(< 7 bytes), some the opposite. Some focus more on performance at the expense of 
more collisions, while some others behave better than the rest on peculiar 
platforms.

Considering all of that, the choice of {hashing func, fingerprinting func} has 
to be up to you.

*For the internal partial hashing that has to happen when reallocating a 
fingerprint server-side, this implementation uses FNV1a which is robust and fast 
for 1 byte inputs (the size of a fingerprint).*

*Thanks to how Cuckoo filters work, that choice is completely transparent to the 
clients.*



Installation 
------------

1. Download a precompiled binary from the [Release section](https://github.com/kristoff-it/redis-cuckoofilter/releases/) of this repo or compile with `make all` (linux and osx supported)

2. Put the `redis-cuckoofilter.so` module in a folder readable by your Redis installation

3. To try out the module you can send `MODULE LOAD /path/to/redis-cuckoofilter.so` using redis-cli or a client of your choice

4. Once you save on disk a key containing a Cuckoo filter you will need to add `loadmodule /path/to/redis-cuckoofilter.so` to your `redis.conf`, otherwise Redis will not load complaining that it doesn't know how to read some data from the `.rdb` file.


Usage Example
-------------

```
redis-cli> CF.INIT test 64K
65536
 
redis-cli> CF.ADD test 5366164415461427448 97
OK

redis-cli> CF.CHECK test 5366164415461427448 97
(integer) 1

redis-cli> CF.REM test 5366164415461427448 97
OK 

redis-cli> CF.CHECK test 5366164415461427448 97
(integer) 0
```

Current API
----------

 (still work in progress)

#### Create a new Cuckoo filter:
`CF.INIT key size`

Example: `CF.INIT test 64K`

- `size`: string representing the size of the filter in bytes; one of {`1K`, `2K`, `4K`, `8K`, `16K`, `32K`, `64K`, `128K`, `256K`, `512K`, `1M`, `2M`, `4M`, `8M`, `16M`, `32M`, `64M`, `128M`, `256M`, `512M`, `1G`, `2G`, `4G`, `8G`}

You can consider the size as both a measure of memory usage and the total number of elements you can put inside the filter before filling it up completely. A 64K filter will (roughly) allocate 64KBytes of memory and will hold up to 65536 elements.

In general, it's a good idea to size the filter so that it doesn't get too full, as it:
- lowers the chance of overcrowding some buckets (you get the `too full` error if you add too many colliding items)
- prevents the insertion time from growing as a fuller filter needs to move around more fingerprints (cf. the linked paper for more information)
- keeps the false positive rate low (e.g. a full filter has a higher error rate than a half-full one). 

That said the filters can be filled up to ~98% if you have good distribuited elements (cf. the linked paper).
	
Replies with the numeric value of the maximum number of elements you can put inside (`64K` -> `65536`)

#### Add an item:
`CF.ADD key hash fp`

Example: `CF.ADD test -8965164415461427448 205`

- `hash`: digest of the original item using a hashing function of your choice encoded as an int64 (**signed** long long).
- `fp`: fingerprint of the original item: 1 byte encoded as 0 - 255

Replies: `OK`

The current implementation is a (2,4) type of Cuckoo filter, meaning that there are 2 possible buckets for each fingerprint and that each bucket can contain 4 of them. If you add more than 8 colliding items (i.e. same hash and fingerprint), you will get a `too full` error.
In those cases you should consider using a bigger filter (a good rule of thumb is to size the filter to be 2x the total number of items you aim to add, so if you were planning to add up to `64K` items, consider using a `128K` filter).

This will unfortunately not solve the use case where you have a multiset and are interested in keeping track of identical copies of the same item (unless you can have up to < 8 copies of the same item in your multiset). There is a plan to also add support for multisets (by adding a specialized type of filter) so you should check its progress status on the bottom of this readme.

#### Remove an item:
`CF.REM key hash fp`

Please keep in mind that you are supposed to call this function only on items that have been inserted in the filter. Trying to delete an item that has never been inserted has a small chance of breaking your filter.
	
Returns an error if the item you're trying to remove doesn't exist.

#### Check if an item is part of the filter:
`CF.CHECK key hash fp`

Replies: `1` if the item seems to be present else `0`

#### Obtain the whole filter:
`CF.DUMP key`

Replies with the raw bytes of the filter.


Planned Features
----------------

- Cuckoo filters for multisets: currently you can add a maximum of `2 * bucketsize` copies of the same element before getting a `too full` error. Making a filter that adds a counter for each bucketslot would create a filter specifically designed for handling multisets. 

- Easy interface: just plain old `cf.easyadd key item`, `cf.easycheck key item`, for when you are only prototyping a solution.

- Dynamic Cuckoo filters: resize instead of failing with `too full`


License
-------

MIT License

Copyright (c) 2017 Loris Cro

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
