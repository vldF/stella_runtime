#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

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

#define MAX_GC_ROOTS (16 * 10)
#define MAX_ALLOC_SIZE (16 * 1000)
#define GC_GEN_COUNT 2

//#define DISABLE_GC

int gc_roots_max_size = 0;
int gc_roots_top = 0;
void **gc_roots[MAX_GC_ROOTS];
void *gc_arena;

void* to_space_start[GC_GEN_COUNT];
void* from_space_start[GC_GEN_COUNT];

void* gc_next;
void *alloc_next;
int gc_current_gen;

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
    if (gc_arena == NULL) {
        gc_arena = malloc(MAX_ALLOC_SIZE * GC_GEN_COUNT);

        alloc_next = gc_arena;

        int gen_half_size = MAX_ALLOC_SIZE / 2;

        from_space_start[0] = gc_arena;
        to_space_start[0] = gc_arena + gen_half_size;

        for (int i = 1; i < GC_GEN_COUNT; i++) {
            from_space_start[i] = from_space_start[i-1] + MAX_ALLOC_SIZE;
            to_space_start[i] = to_space_start[i-1] + MAX_ALLOC_SIZE;
        }
    }

  total_allocated_bytes += size_in_bytes;
  total_allocated_objects += 1;
  max_allocated_bytes = total_allocated_bytes;
  max_allocated_objects = total_allocated_objects;

  gc_collect_all();

  void* result = alloc_next;
  alloc_next += size_in_bytes;

  return result;
}
#endif


void* gc_forward(stella_object* ptr) {
    void *possibleNewObjectAddress = ptr->object_fields[0];
    if (gc_is_pointer_in_to_space(possibleNewObjectAddress)) {
        return possibleNewObjectAddress;
    } else {
        gc_chase(ptr);
        return ptr->object_fields[0];
    }
}

void gc_chase(stella_object *ptr) {
    do {
        stella_object *q = gc_next;
        int fields_count = STELLA_OBJECT_HEADER_FIELD_COUNT(ptr->object_header);
        gc_next += fields_count * sizeof(void*) + sizeof(void *); // todo
        void *r = NULL;

        q->object_header = ptr->object_header;
        for (int i = 0; i < fields_count; i++) {
            q->object_fields[i] = ptr->object_fields[i];

            stella_object *potentially_forwarded = (stella_object *) (q->object_fields[i]);
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
    for (int i = 0; i < GC_GEN_COUNT; i++) {
        gc_current_gen = i;
        gc_collect();
    }

    alloc_next = from_space_start[0]; // todo
}

void gc_collect() {
    gc_next = to_space_start[gc_current_gen];
    void* scan = gc_next;

    for (int root_i = 0; root_i < gc_roots_top; root_i++) {
        void **root_ptr = gc_roots[root_i];
        *root_ptr = gc_forward(*root_ptr);
    }

    while (scan < gc_next) {
        stella_object *obj = scan;
        int fields_count = STELLA_OBJECT_HEADER_FIELD_COUNT(obj->object_header);
        for (int field_i = 0; field_i < fields_count; field_i++) {
            obj->object_fields[field_i] = gc_forward(obj->object_fields[field_i]);
        }

        scan += fields_count * sizeof(void*) + sizeof(void*);
    }

    gc_clean_from_space(); // todo: debug only
    swap_spaces();
}

bool gc_is_pointer_in_to_space(void* ptr) {
    return ptr >= to_space_start[gc_current_gen] && ptr < (to_space_start[gc_current_gen] + MAX_ALLOC_SIZE / 2);
}

bool gc_is_pointer_in_from_space(void* ptr) {
    return ptr >= from_space_start[gc_current_gen] && ptr < (from_space_start[gc_current_gen] + MAX_ALLOC_SIZE / 2);
}

void swap_spaces() {
    void* tmp = from_space_start[gc_current_gen];
    from_space_start[gc_current_gen] = to_space_start[gc_current_gen];
    to_space_start[gc_current_gen] = tmp;
}

void gc_clean_from_space() {
    void *from_start = from_space_start[gc_current_gen];
    for (void* i = from_start; i < from_start + MAX_ALLOC_SIZE / 2; i++) {
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
  total_reads += 1;
}

void gc_write_barrier(void *object, int field_index, void *contents) {
  total_writes += 1;
}

void gc_push_root(void **ptr){
  gc_roots[gc_roots_top++] = ptr;
  if (gc_roots_top > gc_roots_max_size) { gc_roots_max_size = gc_roots_top; }
}

void gc_pop_root(void **ptr){
  gc_roots_top--;
}
