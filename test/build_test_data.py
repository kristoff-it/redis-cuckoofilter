import random, xxhash

random.seed(123)

nORs = ["u64", "char *"][1]
FPLEN = 1

if nORs == "char *":
    def fp(x, num):
        return int.from_bytes(x.to_bytes(8, byteorder='little')[:num], "little") or 1
    def h(x):
        return toSigned(xxhash.xxh64(str(x)).intdigest())
else:
    def fp(x, num):
        return str(int.from_bytes(x.to_bytes(8, byteorder='little')[:num], "little") or 1)
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

GOODH = [h(x) for x in correct_items]
GOODF = [fp(x, FPLEN) for x in correct_items]
WRONGH = [h(x) for x in wrong_items]
WRONGF = [fp(x, FPLEN) for x in wrong_items]
DELETEDH = [h(x) for x in deleted_items]
DELETEDF = [fp(x, FPLEN) for x in deleted_items]

if __name__ == '__main__':
    with open("test-data.c", "w") as file:
        file.write("{} goodItemsH[62000] = {{ {} }};\n".format(nORs,
            ", ".join(['"' + str(x) + '"' for x in GOODH])))
        file.write("{} goodItemsF[62000] = {{ {} }};\n".format(nORs,
            ", ".join(['"' + str(x) + '"' for x in GOODF])))

        file.write("{} wrongItemsH[124000] = {{ {} }};\n".format(nORs,
            ", ".join(['"' + str(x) + '"' for x in WRONGH])))
        file.write("{} wrongItemsF[124000] = {{ {} }};\n".format(nORs,
            ", ".join(['"' + str(x) + '"' for x in WRONGF])))

        file.write("{} deletedItemsH[31000] = {{ {} }};\n".format(nORs,
            ", ".join(['"' + str(x) + '"' for x in DELETEDH])))
        file.write("{} deletedItemsF[31000] = {{ {} }};\n".format(nORs,
            ", ".join(['"' + str(x) + '"' for x in DELETEDF])))

    print("Test sets constructed!")
    print("These sets are meant to be used in the test functions, as generating sets of random numbers is a chore in C.")
    print("To run the tests you don't need to run this function.")
    print("")