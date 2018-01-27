import fasteval
import time


def test1(a):
    return


def test2(a):
    return a * 7 + 2


def run1():
    st = time.time()
    for i in range(10000):
        test1(55)
    print(time.time() - st)


def run2():
    st = time.time()
    for i in range(10000):
        test2(55)
    print(time.time() - st)


def run3():
    st = time.time()
    fasteval.enable()
    for i in range(10000):
        test2(55)
    fasteval.disable()
    print(time.time() - st)


if __name__ == "__main__":
    print(run1())
    print(run2())
    print(run3())

