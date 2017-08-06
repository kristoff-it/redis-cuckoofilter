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
# from test.build_test_data import GOODH, GOODF, WRONGH, WRONGF, DELETEDH, DELETEDF
from cffi import FFI

# Get access to C's rand() function and seed it as we did in the module.
ffi = FFI()

ffi.cdef("""\
int rand();
void srand(int seed);
""")

C = ffi.dlopen(None)
C.srand(42)


def FNV1A(x, fpsize):
    xbytes = list(x.to_bytes(4, "little"))

    h = 14695981039346656037
    for i in range(fpsize):
        h = (h ^ xbytes[i]) * 1099511628211
    return h


class RedisCuckooFilter:
    def __init__(self, size, fpsize):
        assert fpSize in [1,2,4]
        assert size % 2 == 0

        self.size = size
        self.fpSize = fpsize

        bucket_generator = lambda: [0,0] if fpsize == 4 else [0,0,0,0]
        bucketsize = 4
        if fpsize != 1:
            bucketsize = 8

        self.numBuckets = size//bucketsize
        self.filter = [bucket_generator() for _ in range(self.numBuckets)]

    def add(self, hashString, fpString):
        h = ctypes.c_ulonglong(int(hashString)).value % self.numBuckets
        fp = ctypes.c_ulonglong(int(fpString)).value

        altH = self._alternative_hash(h, fp)
        
        if self._insert_fp(h, fp, None) or self._insert_fp(altH, fp, None):
            return "OK"

        # This is used to mimic what in C would be passing a "OUT" parameter to
        # a function (ie a pointer to write into).
        homelessFPContainer = []
        homelessH = altH

        for _ in range(500):
            self._insert_fp(homelessH, fp, homelessFPContainer)
            if len(homelessFPContainer) == 0:
                return "OK"

            homelessH = self._alternative_hash(homelessH, homelessFPContainer[0])
            fp = homelessFPContainer[0]
            homelessFPContainer = []

        return "ERR too full"

    def check(self, hashString, fpString):
        h = ctypes.c_ulonglong(int(hashString)).value % self.numBuckets
        fp = ctypes.c_ulonglong(int(fpString)).value

        bucket = self._read_bucket(h)
        altbucket = self._read_bucket(self._alternative_hash(h, fp))

        if fp in bucket or fp in altbucket:
            return True

        return False


    def rem(self, hashString, fpString):
        h = ctypes.c_ulonglong(int(hashString)).value % self.numBuckets
        fp = ctypes.c_ulonglong(int(fpString)).value

        bucket = self._read_bucket(h)
        altbucket = self._read_bucket(self._alternative_hash(h, fp))

        if fp in bucket:
            bucket[bucket.index(fp)] = 0
            return "OK"

        if fp in altbucket:
            altbucket[altbucket.index(fp)] = 0
            return "OK"

        return "ERR trying to delete non existing item. THE FILTER MIGHT BE CORRUPTED!"

    def dump(self):
        return bytes([x for bucket in self.filter for x in bucket])

    def _alternative_hash(self, h, fp):
        return (h ^ FNV1A(fp, self.fpSize)) % self.numBuckets

    def _insert_fp(self, h, fp, homelessFPContainer):
        bucket = self._read_bucket(h)

        if 0 in bucket:
            for i in range(len(bucket)):
                if bucket[i] == 0:
                    bucket[i] = fp
                    return  True

        if homelessFPContainer is not None:
            slot = C.rand() % self.fpSize
            homelessFPContainer.append(bucket[slot])
            bucket[slot] = fp

        return False

    def _read_bucket(self, h):
        return self.filter[h]













