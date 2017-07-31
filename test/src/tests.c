#include "maintest.c"


// Returns 1 if a test failed.
int RunAllTests(RedisModuleCtx *ctx) {
	if (mainTest(ctx)) return 1;

	return 0;
}

void CleanupAllTests(RedisModuleCtx *ctx) {
	cleanMainTest(ctx);
}