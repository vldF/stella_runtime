#!/bin/bash

(docker run -i fizruk/stella compile < "$1") > out.c

sleep 2;

(gcc -std=c11 $2 out.c stella/runtime.c stella/gc.c -o a.out) &&./a.out
