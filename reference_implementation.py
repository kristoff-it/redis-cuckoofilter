# MIT License
#
# Copyright (c) 2017 Loris Cro
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.


# This python script contains the reference implementation used to check che C code.
# While the python version should be extremely less efficient, both implementations
# should output the same exact data. More specifically, the python version uses Lists
# where C uses a single array, but the contents should be equal in both versions.
# Since Cuckoo filters have a randomized step, the python script uses CFFI to obtain
# access to C's rand() and both worlds are seeded with the same value.

import ctypes
from cffi import FFI

# Get access to C's rand() function and seed it as we did in the module.
ffi = FFI()

ffi.cdef("""\
int rand();
void srand(int seed);
""")

C = ffi.dlopen(None)


def FNV1A(fp):
    h = 14695981039346656037
    for byte in fp:
        h = (h ^ byte) * 1099511628211
    return h & 0xffffffffffffffff


class RedisCuckooFilter:
    def __init__(self, size, fpsize):
        assert fpsize in [1,2,4]
        assert size % 2 == 0

        C.srand(42)

        self.size = size
        self.fpSize = fpsize
        self.zeroValue = b'\x00' * self.fpSize

        bucket_generator = lambda: [self.zeroValue] * (2 if self.fpSize == 4 else 4)

        bucketsize = 4 if fpsize == 1 else 8

        self.numBuckets = size//bucketsize
        self.filter = [bucket_generator() for _ in range(self.numBuckets)]

    def add(self, hashString, fpString):
        h = ctypes.c_ulonglong(int(hashString)).value % self.numBuckets
        fp = ctypes.c_ulonglong(int(fpString)).value.to_bytes(8, "little")[:self.fpSize]
        if fp == self.zeroValue:
            fp = ctypes.c_ulonglong(1).value.to_bytes(8, "little")[:self.fpSize]

        altH = self._alternative_hash(h, fp)
        if self._insert_fp(h, fp)[0] or self._insert_fp(altH, fp)[0]:
            return b"OK"

        homelessFP = None
        homelessH = altH

        for _ in range(500):
            _, homelessFP = self._insert_fp(homelessH, fp, evict=True)
            if homelessFP is None:
                return b"OK"

            homelessH = self._alternative_hash(homelessH, homelessFP)
            fp = homelessFP

        return b"ERR too full"

    def check(self, hashString, fpString):
        h = ctypes.c_ulonglong(int(hashString)).value % self.numBuckets
        fp = ctypes.c_ulonglong(int(fpString)).value.to_bytes(8, "little")[:self.fpSize]
        if fp == self.zeroValue:
            fp = ctypes.c_ulonglong(1).value.to_bytes(8, "little")[:self.fpSize]

        bucket = self._read_bucket(h)
        altbucket = self._read_bucket(self._alternative_hash(h, fp))

        if fp in bucket or fp in altbucket:
            return True

        return False


    def rem(self, hashString, fpString):
        h = ctypes.c_ulonglong(int(hashString)).value % self.numBuckets
        fp = ctypes.c_ulonglong(int(fpString)).value.to_bytes(8, "little")[:self.fpSize]
        if fp == self.zeroValue:
            fp = ctypes.c_ulonglong(1).value.to_bytes(8, "little")[:self.fpSize]

        bucket = self._read_bucket(h)
        altbucket = self._read_bucket(self._alternative_hash(h, fp))

        if fp in bucket:
            bucket[bucket.index(fp)] = self.zeroValue
            return b"OK"

        if fp in altbucket:
            altbucket[altbucket.index(fp)] = self.zeroValue
            return b"OK"

        return b"ERR trying to delete non existing item. THE FILTER MIGHT BE CORRUPTED!"

    def dump(self):
        return b''.join(fp for bucket in self.filter for fp in bucket)

    def _alternative_hash(self, h, fp):
        return (h ^ FNV1A(fp)) % self.numBuckets

    def _insert_fp(self, h, fp, evict=False):
        bucket = self._read_bucket(h)

        if self.zeroValue in bucket:
            bucket[bucket.index(self.zeroValue)] = fp
            return  (True, None)

        if evict:
            slot = C.rand() % (2 if self.fpSize == 4 else 4)
            evictedFP = bucket[slot]
            bucket[slot] = fp
            return (False, evictedFP)
        else:
            return (False, None)

    def _read_bucket(self, h):
        return self.filter[h]













