import random
random.seed(123)

correct_items = set()
wrong_items = set()

while len(correct_items) < 62_000:
    correct_items.add(random.randint(1_000_000, 1_000_000_000))

print("Good set constructed!")
with open("good_items", "w") as file:
    file.write(", ".join([str(x) + "LL" for x in correct_items]))


while len(wrong_items) < 50_000:
    randElem = random.randint(50_000_000_000, 100_000_000_000)
    if randElem not in correct_items:
        wrong_items.add(randElem)

with open("wrong_items", "w") as file:
    file.write(", ".join([str(x) + "LL" for x in wrong_items]))

deleted_items = list(correct_items)
random.shuffle(deleted_items)
deleted_items = deleted_items[:int(len(correct_items)/2)]
with open("deleted_items", "w") as file:
    file.write(", ".join([str(x) + "LL" for x in deleted_items]))


print("Test sets constructed!")
print("These sets are meant to be used in the test functions, as generating sets of random numbers is a chore in C.")
print("To run the tests you don't need to run this function.")
