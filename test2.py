import fasteval

N = 100000

def test(a):
    b = 0
    for i in a:
        b = b + i
    return b

def run_normal():
    test(range(N))

def run_fasteval():
    fasteval.enable()
    test(range(N))
    fasteval.disable()


if __name__ == "__main__":
    import time
    st = time.time()
    run_normal()
    print(time.time() - st)
    st = time.time()
    run_fasteval()
    print(time.time() - st)
