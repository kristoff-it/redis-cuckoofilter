import redis, random, xxhash
from ctypes import c_longlong as ll
from tqdm import tqdm

BATCH_SIZE = 10_000

correct_items = set()
wrong_items = set()

filterSize = "64K"
random.seed(123)
while len(correct_items) < 62_000:
    correct_items.add(random.randint(1_000_000, 1_000_000_000))

print("Good set constructed!")


while len(wrong_items) < 50_000:
    randElem = random.randint(50_000_000_000, 100_000_000_000)
    if randElem not in correct_items:
        wrong_items.add(randElem)

print("Test sets constructed!")

r = redis.StrictRedis(host='localhost', port=6379, db=0)

print("Deleting existing Filter:", r.execute_command('del', 'lol'))
print("Creating Filter:", r.execute_command('cf.init', 'lol', filterSize))
pipe = r.pipeline()
i = 0
for elem in tqdm(correct_items):
    strNum = str(elem)
    iD = ll(xxhash.xxh64(strNum).intdigest()).value
    last8bits = (elem).to_bytes(8, byteorder='big')[-1]
    if last8bits == 0:
        last8bits += 1
    command = ('cf.add', 'lol', iD, last8bits)
    pipe.execute_command(*command)
    i += 1
    if i % BATCH_SIZE == 0:
        pipe.execute()
        pipe = r.pipeline()
pipe.execute()

print("Added all good elements, now checking!")

right = 0
pipe = r.pipeline()
i = 0
for elem in tqdm(correct_items):
    strNum = str(elem)
    iD = ll(xxhash.xxh64(strNum).intdigest()).value
    last8bits = (elem).to_bytes(8, byteorder='big')[-1]
    if last8bits == 0:
        last8bits = 1
    command = ('cf.check', 'lol', iD, last8bits)
    pipe.execute_command(*command)
    i += 1
    if i % BATCH_SIZE == 0:
        right += sum(pipe.execute())
        pipe = r.pipeline()
right += sum(pipe.execute())

print("Recollection (must be 100%):",  (right/len(correct_items)) * 100, '%')

wrong = 0
for elem in tqdm(wrong_items):
    strNum = str(elem)
    iD = ll(xxhash.xxh64(strNum).intdigest()).value
    last8bits = (elem).to_bytes(8, byteorder='big')[-1]
    if last8bits == 0:
        last8bits += 1
    command = ('cf.check', 'lol', iD, last8bits)
    wrong += r.execute_command(*command)
print("False positive rate:",  (wrong/len(wrong_items)) * 100, '%')


deleted_items = list(correct_items)
random.shuffle(deleted_items)
deleted_items = deleted_items[:int(len(correct_items)/2)]

print("Deleting half of the good items.")

i = 0
pipe = r.pipeline()
for elem in tqdm(deleted_items):
    strNum = str(elem)
    iD = ll(xxhash.xxh64(strNum).intdigest()).value
    last8bits = (elem).to_bytes(8, byteorder='big')[-1]
    if last8bits == 0:
        last8bits += 1
    command = ('cf.rem', 'lol', iD, last8bits)
    pipe.execute_command(*command)
    i += 1
    if i % BATCH_SIZE == 0:
        pipe.execute()
        pipe = r.pipeline()
pipe.execute()

print("Deleted half of the good items, checking!")

wrong = len(deleted_items)
pipe = r.pipeline()
i = 0
for elem in tqdm(deleted_items):
    strNum = str(elem)
    iD = ll(xxhash.xxh64(strNum).intdigest()).value
    last8bits = (elem).to_bytes(8, byteorder='big')[-1]
    if last8bits == 0:
        last8bits += 1
    command = ('cf.check', 'lol', iD, last8bits)
    pipe.execute_command(*command)
    i += 1
    if i % BATCH_SIZE == 0:
        wrong -= sum(pipe.execute())
        pipe = r.pipeline()
wrong -= sum(pipe.execute())


print("Correctly forgotten (still subject to intrinsic false-positive):",  (wrong/len(deleted_items)) * 100, '%')

i = 0
pipe = r.pipeline()
for elem in tqdm(deleted_items):
    strNum = str(elem)
    iD = ll(xxhash.xxh64(strNum).intdigest()).value
    last8bits = (elem).to_bytes(8, byteorder='big')[-1]
    if last8bits == 0:
        last8bits += 1
    command = ('cf.add', 'lol', iD, last8bits)
    pipe.execute_command(*command)
    i += 1
    if i % BATCH_SIZE == 0:
        pipe.execute()
        pipe = r.pipeline()
pipe.execute()

print("Added the deleted items back in")

right = 0
pipe = r.pipeline()
i = 0
for elem in tqdm(correct_items):
    strNum = str(elem)
    iD = ll(xxhash.xxh64(strNum).intdigest()).value
    last8bits = (elem).to_bytes(8, byteorder='big')[-1]
    if last8bits == 0:
        last8bits = 1
    command = ('cf.check', 'lol', iD, last8bits)
    pipe.execute_command(*command)
    i += 1
    if i % BATCH_SIZE == 0:
        right += sum(pipe.execute())
        pipe = r.pipeline()
right += sum(pipe.execute())

print("Recollection (must be 100%):",  (right/len(correct_items)) * 100, '%')

