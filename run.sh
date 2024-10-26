#!/bin/bash

(docker run -i fizruk/stella compile < "$1") > out.c

sleep 2;

(gcc -std=c11 -DSTELLA_DEBUG -DSTELLA_GC_STATS -DSTELLA_RUNTIME_STATS out.c stella/runtime.c stella/gc.c -o a.out) &&./a.out
