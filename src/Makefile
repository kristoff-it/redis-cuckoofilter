# (kristoff-it): I don't really know how to write Makefiles, 
#                I'm just parroting from antirez/neural-redis.


# find the OS
uname_S := $(shell sh -c 'uname -s 2>/dev/null || echo not')

# Compile flags for linux / osx
ifeq ($(uname_S),Linux)
	SHOBJ_CFLAGS ?= -W -Wall -fno-common -g -ggdb -std=c99 -O3
	SHOBJ_LDFLAGS ?= -shared
else
	SHOBJ_CFLAGS ?= -W -Wall -dynamic -fno-common -g -ggdb -std=c99 -O3
	SHOBJ_LDFLAGS ?= -bundle -undefined dynamic_lookup 
endif

.SUFFIXES: .c .so .xo .o

all: redis-cuckoofilter.so 

.c.xo:
	$(CC) -I. $(CFLAGS) $(SHOBJ_CFLAGS) -fPIC -c $< -o $@

cuckoofilter.c: headers/cuckoofilter.h

redis-cuckoofilter.xo: redismodule.h

redis-cuckoofilter.so: redis-cuckoofilter.xo cuckoofilter.xo
	$(LD) -o $@ cuckoofilter.xo $< $(SHOBJ_LDFLAGS) $(LIBS) -lc

clean:
	rm -rf *.xo *.so