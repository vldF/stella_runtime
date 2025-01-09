## Stella Runtime

### How to run

#### Tests

There are some automatic E2E tests. They compile some stella program with and without GC, then run it with
some argument (based on a test code) and assert the output is the same

```shell
python3 ./tests.py
```

#### Manual compilation with CMAke

Create `./out.c` and put the compiled stella C code to it. Then run the following commands:

```shell
mkdir build
cd build
cmake ..
make
./stella_runtime
```

You can also turn on debug defines in cmake, see `./CMakeLists.txt`

#### With run.sh script

Run:

```shell
sh ./run.sh <path to .st file> [gcc args]
```

For example,

```shell
sh ./run.sh ./testdata/fib.st -DSTELLA_DEBUG -DSTELLA_GC_STATS -DSTELLA_RUNTIME_STATS
```

see `./run.sh` for more details

You can use `MAX_ALLOC_SIZE` macro to define the size of the first generation (288 bytes by default). Also, you can 
define `MAX_GC_ROOTS` macro to set the maximum number of roots on a stack. You can define `DISABLE_GC` macro to disable
the GC. In this case, the simpler allocator will be used. `GC_GEN_COUNT` allows you to set the number of generations
to be used.


### Statistics

#### print_gc_alloc_stats

An example:

```
Garbage collector (GC) statistics:
Total memory allocation: 448 bytes (24 objects)
Maximum residency:       448 bytes (24 objects)
Total memory use:        42 reads and 0 writes
Max GC roots stack size: 22 roots
GC total runs count:     2
gen 0:
 GC runs count:          2
 gen max allocated:      272
gen 1:
 GC runs count:          0
 gen max allocated:      424
```

The first part contains basic memory statistics, the second one contains the statistics over generations. For each
generation it contains GC runs count and maximum allocated memory size. 

#### print_gc_state

An example:

```
ROOTS: 0x16f28b4e8 0x16f28b4e0 0x16f28b4f0 0x16f28b498 0x16f28b490 
0x16f28b488 0x16f28b480 0x16f28b4a0 0x16f28b428 0x16f28b420 0x16f28b418 
0x16f28b410 0x16f28b430 0x16f28b3c8 0x16f28b3c0 0x16f28b3b8 0x16f28b368 
0x16f28b360 0x16f28b358 0x16f28b350 0x16f28b370 


===generation 0 [from space from=0x12b008200 to=0x12b008320]===
 space start=0x12b008200
 space next=0x12b0082e0
 gen scan=0x0

allocated 224/288
free 64/288
object #0 0x12b008200
 GC object header: new_ptr = 0x0
 field 0 has ptr 0x100b80180, it's value is 0
object #1 0x12b008218
 GC object header: new_ptr = 0x0
 field 0 has ptr 0x12b008208, it's value is 1
object #2 0x12b008230
 GC object header: new_ptr = 0x0
 field 0 has ptr 0x12b008220, it's value is 2
object #3 0x12b008248
 GC object header: new_ptr = 0x0
 field 0 has ptr 0x12b008238, it's value is 3
 field 1 has ptr 0x12b008238, it's value is 3
 field 2 has ptr 0x12b008238, it's value is 3
object #4 0x12b008270
 GC object header: new_ptr = 0x0
 field 0 has ptr 0x100b80180, it's value is 0
object #5 0x12b008288
 GC object header: new_ptr = 0x0
 field 0 has ptr 0x100b778a8, it's value is 
object #6 0x12b0082a0
 GC object header: new_ptr = 0x0
 field 0 has ptr 0x100b776e0, it's value is 
object #7 0x12b0082b8
 GC object header: new_ptr = 0x0
object #8 0x12b0082c8
 GC object header: new_ptr = 0x0
object #9 0x12b0082d8
 GC object header: new_ptr = 0x0

===generation 1 [from space from=0x12b008320 to=0x12b0087a0]===
 space start=0x12b008320
 space next=0x12b008320
 gen scan=0x0

allocated 0/1152
free 1152/1152
```

The first paragraph contains current roots pointers on a stack. Then, information about each of 
two generations is presented. It contains start and end pointers of a generation, a pointer for the next allocation,
the `scan` variable value. It also contains some information about each object in the generation currently presented.
Each object's description contains `new_ptr` value shows the new position of the object after forwarding process. 
Additionally, it contains the object pointer and its fields values. 
