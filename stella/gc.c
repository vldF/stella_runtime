#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <assert.h>

#include "runtime.h"
#include "gc.h"

/** Total allocated number of bytes (over the entire duration of the program). */
size_t total_allocated_bytes = 0;

/** Total allocated number of objects (over the entire duration of the program). */
int total_allocated_objects = 0;

size_t max_allocated_bytes = 0;
size_t max_allocated_objects = 0;

int total_reads = 0;
int total_writes = 0;

#define MAX_GC_ROOTS 1024
#define MAX_ALLOC_SIZE (16 * 16)
#define GC_GEN_COUNT 2

//#define DISABLE_GC

int gc_roots_max_size = 0;
int gc_roots_top = 0;
void **gc_roots[MAX_GC_ROOTS];

void *gc_from_space;
void *gc_from_space_next;
void *gc_to_space;
void *gc_to_space_next;

void gc_chase(stella_object *ptr);
void gc_collect();
void gc_collect_all();
void gc_clean_from_space();
bool gc_is_pointer_in_to_space(void* ptr);
bool gc_is_pointer_in_from_space(void* ptr);
void swap_spaces();

#ifdef DISABLE_GC
void* gc_alloc(size_t size_in_bytes) {
    total_allocated_bytes += size_in_bytes;
    total_allocated_objects += 1;
    max_allocated_bytes = total_allocated_bytes;
    max_allocated_objects = total_allocated_objects;

    return malloc(size_in_bytes);
}
#endif

#ifndef DISABLE_GC
void* gc_alloc(size_t size_in_bytes) {
    if (gc_from_space_next == NULL) {
        gc_from_space = malloc(MAX_ALLOC_SIZE * 2);
        gc_from_space_next = gc_from_space;

        gc_to_space = gc_from_space + MAX_ALLOC_SIZE;
        gc_to_space_next = gc_to_space;
    }

  total_allocated_bytes += size_in_bytes;
  total_allocated_objects += 1;
  max_allocated_bytes = total_allocated_bytes;
  max_allocated_objects = total_allocated_objects;

  gc_collect_all();

  void* result = gc_from_space_next;
  gc_from_space_next += size_in_bytes;

  return result;
}
#endif


void* gc_forward(stella_object* ptr) {
    if (gc_is_pointer_in_from_space(ptr)) {
        void *possibleNewObjectAddress = ptr->object_fields[0];
        if (gc_is_pointer_in_to_space(possibleNewObjectAddress)) {
            return possibleNewObjectAddress;
        } else {
            gc_chase(ptr);
            assert(gc_is_pointer_in_to_space(ptr->object_fields[0]));
            return ptr->object_fields[0];
        }
    }

    return ptr;
}

void gc_chase(stella_object *ptr) {
    do {
        stella_object *q = gc_to_space_next;
        int fields_count = STELLA_OBJECT_HEADER_FIELD_COUNT(ptr->object_header);
        gc_to_space_next += fields_count * sizeof(void*) + sizeof(void *); // todo
        void *r = NULL;

        q->object_header = ptr->object_header;
        for (int i = 0; i < fields_count; i++) {
            q->object_fields[i] = ptr->object_fields[i];

            stella_object *potentially_forwarded = q->object_fields[i];
            if (gc_is_pointer_in_from_space(q->object_fields[i])
                    && !gc_is_pointer_in_to_space(potentially_forwarded->object_fields[0])) {
                r = potentially_forwarded;
            }
        }

        ptr->object_fields[0] = q;
        ptr = r;
    } while(ptr != NULL);
}

void gc_collect_all() {
    gc_collect();

    for (int i = 0; i < gc_roots_top; i++) {
        assert(!gc_is_pointer_in_to_space(*gc_roots[i]));
    }
}

void gc_collect() {
    void* scan = gc_to_space_next;

    for (int root_i = 0; root_i < gc_roots_top; root_i++) {
        void **root_ptr = gc_roots[root_i];
        *root_ptr = gc_forward(*root_ptr);
    }

    while (scan < gc_to_space_next) {
        stella_object *obj = scan;
        int fields_count = STELLA_OBJECT_HEADER_FIELD_COUNT(obj->object_header);
        for (int field_i = 0; field_i < fields_count; field_i++) {
            obj->object_fields[field_i] = gc_forward(obj->object_fields[field_i]);
        }

        scan += fields_count * sizeof(void*) + sizeof(void*);
    }

//    // todo: debug only
//    void *t = gc_to_space;
//    while (t < scan) {
//        stella_object *o = t;
//        int fields_count = STELLA_OBJECT_HEADER_FIELD_COUNT(o->object_header);
//        for (int i = 0; i < fields_count; i++) {
//            assert(!gc_is_pointer_in_from_space(STELLA_OBJECT_READ_FIELD(o, i)));
//        }
//
//        t += fields_count * sizeof(void*) + sizeof(void*);
//    }

    gc_clean_from_space(); // todo: debug only
    swap_spaces();
}

bool gc_is_pointer_in_to_space(void* ptr) {
    return ptr >= gc_to_space && ptr < (gc_to_space + MAX_ALLOC_SIZE);
}

bool gc_is_pointer_in_from_space(void* ptr) {
    return ptr >= gc_from_space && ptr < (gc_from_space + MAX_ALLOC_SIZE);
}

void swap_spaces() {
    void* tmp = gc_from_space;
    gc_from_space = gc_to_space;
    gc_to_space = tmp;

    gc_from_space_next = gc_to_space_next;
    gc_to_space_next = gc_to_space;
}

void gc_clean_from_space() {
    for (void* i = gc_from_space; i < gc_from_space + MAX_ALLOC_SIZE; i++) {
        *(char *)i = 0;
    }
}


void print_gc_roots() {
  printf("ROOTS: ");
  for (int i = 0; i < gc_roots_top; i++) {
    printf("%p ", gc_roots[i]);
  }
  printf("\n");
}

void print_gc_alloc_stats() {
  printf("Total memory allocation: %'d bytes (%'d objects)\n", total_allocated_bytes, total_allocated_objects);
  printf("Maximum residency:       %'d bytes (%'d objects)\n", max_allocated_bytes, max_allocated_objects);
  printf("Total memory use:        %'d reads and %'d writes\n", total_reads, total_writes);
  printf("Max GC roots stack size: %'d roots\n", gc_roots_max_size);
}

void print_gc_state() {
  // TODO: not implemented
}

void gc_read_barrier(void *object, int field_index) {
    assert(!gc_is_pointer_in_to_space(object));
  total_reads += 1;
}

void gc_write_barrier(void *object, int field_index, void *contents) {
    assert(!gc_is_pointer_in_to_space(object));
  total_writes += 1;
}

void gc_push_root(void **ptr){
  gc_roots[gc_roots_top++] = ptr;
  if (gc_roots_top > gc_roots_max_size) { gc_roots_max_size = gc_roots_top; }
}

void gc_pop_root(void **ptr){
  gc_roots_top--;
}
