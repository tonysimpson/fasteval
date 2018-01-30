import fasteval
import time

N = 10000000
def test1(a):
    return


def test2(a):
    return a * 7 + 2


def run1():
    st = time.time()
    for i in range(N):
        test1(55)
    return time.time() - st


def run2():
    st = time.time()
    for i in range(N):
        test2(55)
    return time.time() - st


def run3():
    st = time.time()
    fasteval.enable()
    for i in range(N):
        test1(55)
    fasteval.disable()
    return time.time() - st
    

def run4():
    st = time.time()
    fasteval.enable()
    for i in range(N):
        test2(55)
    fasteval.disable()
    return time.time() - st


if __name__ == "__main__":
    base1 = run1()
    orig = run2()
    base2 = run3()
    new = run4()
    print('Speedup %.3f %.3f' % (orig / new, base1 / base2))

