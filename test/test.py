import redis, random, xxhash
from ctypes import c_longlong as ll
from tqdm import tqdm

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
print("Creating Filter:", r.execute_command('cf.init', 'lol', filterSize, 4))
# pipe = r.pipeline()
# i = 0
# for elem in tqdm(correct_items):
#     strNum = str(elem)
#     iD = ll(xxhash.xxh64(strNum).intdigest()).value
#     last8bits = (elem).to_bytes(8, byteorder='big')[-1]
#     if last8bits == 0:
#         last8bits += 1
#     fP = bytes(chr(last8bits), 'latin-1')
#     command = ('cf.add', 'lol', iD, fP)
#     pipe.execute_command(*command)
#     i += 1
#     if i % 5_000 == 0:
#         pipe.execute()
#         pipe = r.pipeline()
# pipe.execute()

for elem in tqdm(correct_items):
    strNum = str(elem)
    iD = ll(xxhash.xxh64(strNum).intdigest()).value
    last8bits = (elem).to_bytes(8, byteorder='big')[-1]
    if last8bits == 0:
        last8bits = 1
    command = ('cf.add', 'lol', iD, last8bits)
    r.execute_command(*command)



print("Added all good elements, now checking!")


right = 0
for elem in tqdm(correct_items):
    strNum = str(elem)
    iD = ll(xxhash.xxh64(strNum).intdigest()).value
    last8bits = (elem).to_bytes(8, byteorder='big')[-1]
    if last8bits == 0:
        last8bits = 1
    command = ('cf.check', 'lol', iD, last8bits)
    if r.execute_command(*command) == 0:
        print("Forgot about", elem, iD, last8bits)
    else:
        right += 1

print("Recollection:",  (right/len(correct_items)) * 100, '%')

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
# clen = len(correct_items)
# while len(deleted_items) < clen / 2 :
#     elem = random.choice(list(correct_items))
#     deleted_items.append(elem)
#     correct_items.remove(elem)

# for elem in correct_items:


print("Deleting half of the good items.")

for elem in tqdm(deleted_items):
    strNum = str(elem)
    iD = ll(xxhash.xxh64(strNum).intdigest()).value
    last8bits = (elem).to_bytes(8, byteorder='big')[-1]
    if last8bits == 0:
        last8bits += 1
    command = ('cf.rem', 'lol', iD, last8bits)
    r.execute_command(*command)

print("Deleted half of the good items, checking!")




right = 0
for elem in tqdm(deleted_items):
    strNum = str(elem)
    iD = ll(xxhash.xxh64(strNum).intdigest()).value
    last8bits = (elem).to_bytes(8, byteorder='big')[-1]
    if last8bits == 0:
        last8bits += 1
    command = ('cf.check', 'lol', iD, last8bits)
    if r.execute_command(*command) == 0:
        right += 1

print("Correctly forgotten (still subject to intrinsic false-positive):",  (right/len(deleted_items)) * 100, '%')


