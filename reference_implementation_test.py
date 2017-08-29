import redis, time
from subprocess import Popen
from test.build_test_data import getTestData
from reference_implementation import RedisCuckooFilter
from tqdm import tqdm

# Remote filter's name

TEST_CONFIG = (
	{'FPLEN': 1, 'SIZE': '64K', 'L_SIZE': 65536, 'TESTNUM': 62000}, 
	{'FPLEN': 2, 'SIZE': '128K', 'L_SIZE': 65536 * 2, 'TESTNUM': 62000},
	{'FPLEN': 4, 'SIZE': '256K', 'L_SIZE': 65536 * 2 * 2, 'TESTNUM': 57000},
)

DEFAULT = 2
GOODH, GOODF, WRONGH, WRONGF, DELETEDH, DELETEDF = None, None, None, None, None, None
cf = None
def test_reference_implementation(tc=DEFAULT):
	rfn = "__spec-test-cuckoo-{}__".format(tc)

	global GOODH, GOODF, WRONGH, WRONGF, DELETEDH, DELETEDF, cf
	GOODH, GOODF, WRONGH, WRONGF, DELETEDH, DELETEDF = getTestData(4, TEST_CONFIG[tc]['TESTNUM'])
	# Launch a Redis server
	redis_server = Popen(["redis-server", "--loadmodule", "src/redis-cuckoofilter.so", "--unixsocket", "redis.sock"])
	time.sleep(3)
	try:
		# Get a client connection
		r = redis.StrictRedis(unix_socket_path='./redis.sock')

		# Init a cuckoo filter locally and on redis
		cf = RedisCuckooFilter(TEST_CONFIG[tc]['L_SIZE'], TEST_CONFIG[tc]['FPLEN'])
		r.execute_command("CF.INIT", rfn, TEST_CONFIG[tc]['SIZE'], TEST_CONFIG[tc]['FPLEN'])

		# Now all tests will procede in lockstep:

		# Add all GOOD items in the filter:	
		for h, fp in tqdm(list(zip(GOODH, GOODF))):
			l = cf.add(h, fp)
			rem = r.execute_command("CF.ADD", rfn, h, fp)
			assert(l == rem)
			assert(cf.dump() == r.execute_command("CF.DUMP", rfn))
		print("CF.ADD is coherent!")


		# Check that we remember all of them:
		assert(len(GOODH) == sum(cf.check(h, fp) for h, fp in zip(GOODH, GOODF)))
		pipe = r.pipeline()
		for h, fp in tqdm(list(zip(GOODH, GOODF))):
			pipe.execute_command("CF.CHECK", rfn, h, fp)
		assert(len(GOODH) == sum(pipe.execute()))
		print("CF.CHECK is coherent in remembering insterted items!")


		# Check that we have the same false positives:
		localFalsePositives = 0
		for h, fp in tqdm(list(zip(WRONGH, WRONGF))):
			localResult = cf.check(h, fp)
			remoteResult = r.execute_command("CF.CHECK", rfn, h, fp)
			assert(localResult == remoteResult)
			localFalsePositives += localResult
		print("False positive: {}%".format( (localFalsePositives/len(WRONGH))*100) )
		print("CF.CHECK is coherent in answering about non-existing items!")
		assert((len(WRONGH)/100 * 3) > localFalsePositives)


		# Delete half of the inserted items:
		for h, fp in tqdm(list(zip(DELETEDH, DELETEDF))):
			assert(cf.rem(h, fp) == r.execute_command("CF.REM", rfn, h, fp))
			assert(cf.dump() == r.execute_command("CF.DUMP", rfn))
		print("CF.DELETE is coherent!")

		# Check that we correctly forgot about them:
		localFalsePositives = 0
		for h, fp in tqdm(list(zip(DELETEDH, DELETEDF))):
			localResult = cf.check(h, fp)
			remoteResult = r.execute_command("CF.CHECK", rfn, h, fp)
			assert(localResult == remoteResult)
			localFalsePositives += localResult
		print("False positive: {}%".format( (localFalsePositives/len(DELETEDH))*100) )
		print("CF.CHECK is coherent in anwering about deleted items!")
		assert((len(DELETEDH)/100 * 1.5) > localFalsePositives)

		# Add them back in:
		for h, fp in tqdm(list(zip(DELETEDH, DELETEDF))):
			assert(cf.add(h, fp) == r.execute_command("CF.ADD", rfn, h, fp))
			assert(cf.dump() == r.execute_command("CF.DUMP", rfn))
		print("CF.ADD is coherent in readding items!")


		# Check that we still remember them all:
		assert(len(GOODH) == sum(cf.check(h, fp) for h, fp in zip(GOODH, GOODF)))
		pipe = r.pipeline()
		for h, fp in tqdm(list(zip(GOODH, GOODF))):
			pipe.execute_command("CF.CHECK", rfn, h, fp)
		assert(len(GOODH) == sum(pipe.execute()))
		print("CF.CHECK is coherent!")

		redis_server.kill()
		print("ALL CHECKS PASSED")
		print("THE IMPLEMENTATION *SEEMS* COHERENT WITH THE REFERENCE")
		time.sleep(1)
	except:
		redis_server.kill()
		time.sleep(1)
		print("Byebye Redis!")
		raise

if __name__ == '__main__':
	test_reference_implementation(0)
	test_reference_implementation(1)
	test_reference_implementation(2)

