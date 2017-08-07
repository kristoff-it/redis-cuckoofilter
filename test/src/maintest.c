void cleanMainTest(RedisModuleCtx *ctx) {
	RedisModule_Call(ctx, "del", "c", "__test-cuckoo-filter__");
}

int mainTest(RedisModuleCtx *ctx, char * filterType, char * fpSize) {

#include "../test-data.c"
	
	RedisModuleCallReply *reply;

	reply = RedisModule_Call(ctx, "cf.init", "ccc", "__test-cuckoo-filter__", filterType, fpSize);
	if (RedisModule_CallReplyType(reply) == REDISMODULE_REPLY_INTEGER) {
	    long long testFilterSize = RedisModule_CallReplyInteger(reply);
	    printf("Test filter created: %s %s -> %lli\n", filterType, fpSize, testFilterSize);
	} else {
	    return 1;
	}

	printf("Loading initial items...\n");
	// Load 62k items
	for (int i = 0; i < 62000; i++)
	{
	    reply = RedisModule_Call(ctx, "CF.ADD", "ccc", "__test-cuckoo-filter__", goodItemsH[i], goodItemsF[i]);
	    if (RedisModule_CallReplyType(reply) == REDISMODULE_REPLY_ERROR) {
	        printf("%s", RedisModule_CallReplyStringPtr(reply, NULL));
	        return 1;
	    }
	}
// ------------------
	printf("Checking...\n");
	long long correctCount = 0;
	for (int i = 0; i < 62000; i++)
	{
	    reply = RedisModule_Call(ctx, "CF.CHECK", "ccc", "__test-cuckoo-filter__", goodItemsH[i], goodItemsF[i]);
	    if (RedisModule_CallReplyType(reply) == REDISMODULE_REPLY_INTEGER) {
	        correctCount += RedisModule_CallReplyInteger(reply);
	    } else {
	        printf("%s", RedisModule_CallReplyStringPtr(reply, NULL));
	        return 1;
	    }
	}
	printf("Recollection: %.4f%%\n", (correctCount/62000.0) * 100);
	if ( correctCount == 62000 ) {
	    printf("(TEST: PASSED)\n");
	} else {
	    printf("(TEST: FAILED)\n");
	    return 1;
	}
// ------------------
	printf("Checking for false positives...\n");
	long long wrongCount = 0;
	for (int i = 0; i < 124000; i++)
	{
	    reply = RedisModule_Call(ctx, "CF.CHECK", "ccc", "__test-cuckoo-filter__", wrongItemsH[i], wrongItemsF[i]);
	    if (RedisModule_CallReplyType(reply) == REDISMODULE_REPLY_INTEGER) {
	        i64 x = RedisModule_CallReplyInteger(reply);
	        wrongCount += x;
	    } else {
	        printf("%s", RedisModule_CallReplyStringPtr(reply, NULL));
	        return 1;
	    }
	}
	printf("False positives: %.4f%%\n", (wrongCount/124000.0) * 100);
	if ( wrongCount < (1900 * 2) ) {
	    printf("(TEST: PASSED)\n");
	} else {
	    printf("(TEST: FAILED)\n");
	    return 1;
	}


// -----------
	printf("Deleting half of the good items...\n");
	for (int i = 0; i < 31000; i++)
	{
	    reply = RedisModule_Call(ctx, "CF.REM", "ccc", "__test-cuckoo-filter__", deletedItemsH[i], deletedItemsF[i]);
	    if (RedisModule_CallReplyType(reply) == REDISMODULE_REPLY_ERROR) {
	        printf("%s", RedisModule_CallReplyStringPtr(reply, NULL));
	        return 1;
	    }
	}

	wrongCount = 0;
	for (int i = 0; i < 31000; i++)
	{
	    reply = RedisModule_Call(ctx, "CF.CHECK", "ccc", "__test-cuckoo-filter__", deletedItemsH[i], deletedItemsF[i]);
	    if (RedisModule_CallReplyType(reply) == REDISMODULE_REPLY_INTEGER) {
	        i64 x = RedisModule_CallReplyInteger(reply);
	        wrongCount += x;
	    } else {
	        printf("%s", RedisModule_CallReplyStringPtr(reply, NULL));
	        return 1;
	    }
	}
	printf("Correctly forgotten (subject to false positive error): %.4f%%\n", ((31000 - wrongCount)/31000.0) * 100);
	if ( wrongCount < (950 * 2) ) {
	    printf("(TEST: PASSED)\n");
	} else {
	    printf("(TEST: FAILED)\n");
	    return 1;
	}


// ------------
	printf("Adding those items back in...\n");
	for (int i = 0; i < 31000; i++)
	{
	    reply = RedisModule_Call(ctx, "CF.ADD", "ccc", "__test-cuckoo-filter__", deletedItemsH[i], deletedItemsF[i]);
	    if (RedisModule_CallReplyType(reply) == REDISMODULE_REPLY_ERROR) {
	        printf("%s", RedisModule_CallReplyStringPtr(reply, NULL));
	        return 1;
	    }
	}
	printf("Now checking if we still remember everything...\n");
	correctCount = 0;
	for (int i = 0; i < 62000; i++)
	{
	    reply = RedisModule_Call(ctx, "CF.CHECK", "ccc", "__test-cuckoo-filter__", goodItemsH[i], goodItemsF[i]);
	    if (RedisModule_CallReplyType(reply) == REDISMODULE_REPLY_INTEGER) {
	        correctCount += RedisModule_CallReplyInteger(reply);
	    } else {
	        printf("%s", RedisModule_CallReplyStringPtr(reply, NULL));
	        return 1;
	    }
	}
	printf("Recollection: %.4f%%\n", (correctCount/62000.0) * 100);
	if ( correctCount == 62000 ) {
	    printf("(TEST: PASSED)\n");
	} else {
	    printf("(TEST: FAILED)\n");
	    return 1;
	}

	return 0;
}