import subprocess

testdata_dir = "./testdata/"

def compile(test_name: str):
    with open(testdata_dir + test_name + ".st", "r") as inp:
        with open("test_out.c", "w") as res_file:
            subprocess.call(
                ["docker", "run", "-i", "fizruk/stella", "compile"],
                stdout=res_file,
                shell=False,
                stdin=inp
            )

    subprocess.call(
        ["gcc", "-std=c11", "test_out.c",
         "stella/runtime.c", "stella/gc.c", "-o", "test_out_gc.out"])

    subprocess.call(
        ["gcc", "-std=c11", "-DDISABLE_GC", "test_out.c",
         "stella/runtime.c", "stella/gc.c", "-o", "test_out_no_gc.out"])


def run_with_gc(inp):
    res = subprocess.run(
        ["./test_out_gc.out"],
        input=str.encode(str(inp)),
        capture_output=True,
        text=False,
        timeout=10,
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


if __name__ == "__main__":
    test_fib()
