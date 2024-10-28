import subprocess

testdata_dir = "./testdata/"


def compile(test_name: str, max_heap_size: int = 16 * 512):
    print(test_name)
    with open(testdata_dir + test_name + ".st", "r") as inp:
        with open("test_out.c", "w") as res_file:
            subprocess.call(
                ["docker", "run", "-i", "fizruk/stella", "compile"],
                stdout=res_file,
                shell=False,
                stdin=inp,
                stderr=subprocess.DEVNULL,
            )

    subprocess.call(
        [
            "gcc",
            "-std=c11",
            "-DMAX_ALLOC_SIZE="+str(max_heap_size),
            "test_out.c",
            "stella/runtime.c",
            "stella/gc.c",
            "-o",
            "test_out_gc.out"],
        stderr=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL
    )

    subprocess.call(
        [
            "gcc",
            "-std=c11",
            "-DDISABLE_GC",
            "-DMAX_ALLOC_SIZE="+str(max_heap_size),
            "test_out.c",
            "stella/runtime.c",
            "stella/gc.c",
            "-o",
            "test_out_no_gc.out"
        ],
        stderr=subprocess.DEVNULL,
        stdout=subprocess.DEVNULL
    )


def run_with_gc(inp):
    res = subprocess.run(
        ["./test_out_gc.out"],
        input=str.encode(str(inp)),
        capture_output=True,
        text=False,
        timeout=3,
    )

    res.check_returncode()
    return res.stdout


def run_without_gc(inp):
    res = subprocess.run(
        ["./test_out_no_gc.out"],
        input=str.encode(str(inp)),
        capture_output=True,
        text=False
    )

    res.check_returncode()
    return res.stdout


def do_fib_test(x):
    without_gc = run_without_gc(x)
    with_gc = run_with_gc(x)

    assert with_gc == without_gc, "expected " + str(without_gc) + ", but got " + str(with_gc)


def test_fib():
    testname = "fib"
    compile(testname)

    do_fib_test(1)
    do_fib_test(10)


def do_id_test(x):
    without_gc = run_without_gc(x)
    with_gc = run_with_gc(x)

    assert with_gc == without_gc, "expected " + str(without_gc) + ", but got " + str(with_gc)


def test_id():
    testname = "id"
    compile(testname)

    do_id_test(1)
    do_id_test(10)
    do_id_test(30)
    do_id_test(150)


def do_factorial_test(x):
    without_gc = run_without_gc(x)
    with_gc = run_with_gc(x)

    assert with_gc == without_gc, "expected " + str(without_gc) + ", but got " + str(with_gc)


def test_factorial():
    testname = "factorial"
    compile(testname, max_heap_size=16*1024)

    do_factorial_test(1)
    do_factorial_test(2)
    do_factorial_test(3)
    do_factorial_test(5)


if __name__ == "__main__":
    test_fib()
    test_id()
    test_factorial()
