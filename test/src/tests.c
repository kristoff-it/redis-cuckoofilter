#include "maintest.c"


// Returns 1 if a test failed.
int RunAllTests(RedisModuleCtx *ctx) {
	if (mainTest(ctx, "64K", "1")) return 1;
	cleanMainTest(ctx);
	if (mainTest(ctx, "128K", "2")) return 1;
	cleanMainTest(ctx);
	if (mainTest(ctx, "256K", "4")) return 1;

	printf("\nALL TESTS PASSED!\n");
	return 0;
}

void CleanupAllTests(RedisModuleCtx *ctx) {
	// cleanMainTest(ctx);
}