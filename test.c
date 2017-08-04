#include <stdlib.h>
#include <stdio.h>

typedef unsigned long long u64;


u64 haszero(u64 v) {
    return (v - 0x0101010101010101ULL) & ~v & 0x8080808080808080ULL;
}

u64 hasvalue(u64 x, unsigned char n){
    return haszero(x ^ ~0ULL/255 * n);
}

int main(int argc, char const *argv[])
{
	printf("%llu\n%llu", hasvalue(0xFFFFFFFFFFFFFF08ULL, 255), ~0ULL/255);
	return 0;
}