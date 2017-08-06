import random, xxhash

random.seed(123)

nORs = ["u64", "char *"][1]

if nORs == "char *":
    def fp(x, num):
        return '"' + str(int.from_bytes(x.to_bytes(8, byteorder='little')[:num], "little")) + '"'
    def h(x):
        return '"' + str(toSigned(xxhash.xxh64(str(x)).intdigest())) + '"'
else:
    def fp(x, num):
        return str(int.from_bytes(x.to_bytes(8, byteorder='little')[:num], "little"))
    def h(x):
        return str(toSigned(xxhash.xxh64(str(x)).intdigest())) + 'LL'


def toSigned(n):
    n = n & 0xFFFFFFFFFFFFFFFF
    return (n ^ 0x8000000000000000) - 0x8000000000000000

correct_items = set()
wrong_items = set()




while len(correct_items) < 62_000:
    correct_items.add(random.randint(1_000_000, 1_000_000_000))

print("Good set constructed!")

while len(wrong_items) < 124_000:
    randElem = random.randint(50_000_000_000, 100_000_000_000)
    if randElem not in correct_items:
        wrong_items.add(randElem)


deleted_items = list(correct_items)
random.shuffle(deleted_items)
deleted_items = deleted_items[:int(len(correct_items)/2)]



with open("test-data.c", "w") as file:
    file.write("{} goodItemsH[62000] = {{ {} }};\n".format(nORs,
        ", ".join([h(x) for x in correct_items])))
    file.write("{} goodItemsF[62000] = {{ {} }};\n".format(nORs,
        ", ".join([fp(x, 1) for x in correct_items])))

    file.write("{} wrongItemsH[124000] = {{ {} }};\n".format(nORs,
        ", ".join([h(x) for x in wrong_items])))
    file.write("{} wrongItemsF[124000] = {{ {} }};\n".format(nORs,
        ", ".join([fp(x, 1) for x in wrong_items])))

    file.write("{} deletedItemsH[31000] = {{ {} }};\n".format(nORs,
        ", ".join([h(x) for x in deleted_items])))
    file.write("{} deletedItemsF[31000] = {{ {} }};\n".format(nORs,
        ", ".join([fp(x, 1) for x in deleted_items])))

print("Test sets constructed!")
print("These sets are meant to be used in the test functions, as generating sets of random numbers is a chore in C.")
print("To run the tests you don't need to run this function.")
print("")