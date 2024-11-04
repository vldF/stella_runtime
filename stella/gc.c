#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <assert.h>

#include "runtime.h"
#include "gc.h"

/** GC statistics **/
size_t total_allocated_bytes = 0;
int total_allocated_objects = 0;

size_t max_allocated_bytes = 0;
size_t max_allocated_objects = 0;

long gc_runs_total = 0;

int total_reads = 0;
int total_writes = 0;

#define MAX_GC_ROOTS 1024
#define MAX_ALLOC_SIZE (16 * 200)
#define GC_GEN_COUNT 2
// +1 is for the last generation that is doubled

//#define DISABLE_GC

int gc_roots_max_size = 0;
int gc_roots_top = 0;
void **gc_roots[MAX_GC_ROOTS];
int gc_roots_in_other_gens_top = 0;
void **gc_roots_in_other_gens[MAX_GC_ROOTS];

void *gc_from_space;
void *gc_to_space;
void *gc_to_space_next;

void *gc_generations[GC_GEN_COUNT];
void *gc_last_gen_alternative;
void *gc_generations_next[GC_GEN_COUNT];

void gc_chase(stella_object *ptr);
void gc_collect();
void gc_collect_all();
void gc_collect_last_gen();
void gc_clean_from_space();
void gc_collect_roots();
bool gc_is_pointer_in_to_space(void* ptr);
bool gc_is_pointer_in_from_space(void* ptr);

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
    if (gc_generations[0] == NULL) {
        size_t arena_size = MAX_ALLOC_SIZE * (GC_GEN_COUNT + 1); // +1 is for the last generation that is doubled
        void *arena = malloc(arena_size);

        for (size_t i = 0; i < GC_GEN_COUNT; i++) {
            gc_generations[i] = arena + i * MAX_ALLOC_SIZE;
            gc_generations_next[i] = gc_generations[i];
        }

        gc_last_gen_alternative = malloc(MAX_ALLOC_SIZE * 2); // the last on is doubled
    }

  total_allocated_bytes += size_in_bytes;
  total_allocated_objects += 1;
  max_allocated_bytes = total_allocated_bytes;
  max_allocated_objects = total_allocated_objects;

  if (gc_generations_next[0] + size_in_bytes > gc_generations[0] + MAX_ALLOC_SIZE) {
      gc_collect_all();
  }

  if (gc_generations_next[0] + size_in_bytes > gc_generations[0] + MAX_ALLOC_SIZE) {
      printf("Out Of Memory!");
      exit(137);
  }

  void* result = gc_generations_next[0];
  gc_generations_next[0] += size_in_bytes;

  return result;
}
#endif

void gc_collect_all() {
    gc_collect_last_gen();

    for (int i = GC_GEN_COUNT-1-1; i >= 0; i--) {
        gc_from_space = gc_generations[i];
        gc_to_space = gc_generations_next[i+1];
        gc_to_space_next = gc_to_space;

        gc_collect_roots();

        gc_collect();

        gc_roots_in_other_gens_top = 0;

        gc_generations_next[i] = gc_generations[i];
    }
}

void gc_collect_last_gen() {
    int gen_idx = GC_GEN_COUNT - 1;
    void *last_gen_start = gc_generations[gen_idx];

    gc_from_space = last_gen_start;
    gc_to_space = gc_last_gen_alternative;
    gc_to_space_next = gc_last_gen_alternative;

    gc_collect_roots();

    gc_collect();

    gc_roots_in_other_gens_top = 0;
    
    gc_generations_next[gen_idx] = gc_to_space_next;

    gc_generations[gen_idx] = gc_last_gen_alternative;
    gc_last_gen_alternative = last_gen_start;
}


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
        gc_to_space_next += sizeof(stella_object) + fields_count * sizeof(void*); // todo
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

void gc_collect() {
    gc_runs_total++;

//    size_t allocated_bytes = (gc_from_space_next - gc_from_space);
//    if (allocated_bytes > max_allocated_bytes) {
//        max_allocated_bytes = allocated_bytes;
//    }

    void* scan = gc_to_space;

    for (int root_i = 0; root_i < gc_roots_top; root_i++) {
        void **root_ptr = gc_roots[root_i];
        *root_ptr = gc_forward(*root_ptr);
    }

    for (int root_i = 0; root_i < gc_roots_in_other_gens_top; root_i++) {
        void **root_ptr = gc_roots_in_other_gens[root_i];
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

    gc_clean_from_space(); // todo: debug only
}

bool gc_is_pointer_in_to_space(void* ptr) {
    return ptr >= gc_to_space && ptr < (gc_to_space + MAX_ALLOC_SIZE);
}

bool gc_is_pointer_in_from_space(void* ptr) {
    return ptr >= gc_from_space && ptr < (gc_from_space + MAX_ALLOC_SIZE);
}

void gc_clean_from_space() {
    for (void* i = gc_from_space; i < gc_from_space + MAX_ALLOC_SIZE; i++) {
        *(char *)i = 0;
    }
}

void gc_collect_roots() {
    for (int i = 0; i < GC_GEN_COUNT; i++) {
        void* scan = gc_generations[i];

        if (gc_is_pointer_in_from_space(scan)) {
            continue;
        }

        while (scan < gc_generations_next[0]) {
            stella_object *obj = scan;
            int fields_count = STELLA_OBJECT_HEADER_FIELD_COUNT(obj->object_header);

            for (int field_i = 0; field_i < fields_count; field_i++) {
                void *field_ptr = obj->object_fields[field_i];
                if (gc_is_pointer_in_from_space(field_ptr)) {
                    gc_roots_in_other_gens[gc_roots_in_other_gens_top++] = &obj->object_fields[field_i];
                }
            }

            scan += sizeof(stella_object) + fields_count * sizeof(void*);
        }
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
  printf("Total memory allocation: %'zu bytes (%'d objects)\n", total_allocated_bytes, total_allocated_objects);
  printf("Maximum residency:       %'zu bytes (%'zu objects)\n", max_allocated_bytes, max_allocated_objects);
  printf("Total memory use:        %'d reads and %'d writes\n", total_reads, total_writes);
  printf("Max GC roots stack size: %'d roots\n", gc_roots_max_size);
  printf("GC runs count:           %'zu\n", gc_runs_total);

  for (int i = 0; i < GC_GEN_COUNT; i++) {
      printf("gen %d:\n", i);
      int max = (i + 1) == GC_GEN_COUNT ? 2 * MAX_ALLOC_SIZE : MAX_ALLOC_SIZE;
      printf(" allocated %zu/%d\n", (gc_generations_next[i] - gc_generations[i]), max);
  }
}

void print_gc_state() {
  // TODO: not implemented
}

void gc_read_barrier(void *object, int field_index) {
//    assert(!gc_is_pointer_in_to_space(object));
  total_reads += 1;
}

void gc_write_barrier(void *object, int field_index, void *contents) {
//    assert(!gc_is_pointer_in_to_space(object));
  total_writes += 1;
}

void gc_push_root(void **ptr){
  gc_roots[gc_roots_top++] = ptr;
  if (gc_roots_top > gc_roots_max_size) { gc_roots_max_size = gc_roots_top; }
}

void gc_pop_root(void **ptr){
  gc_roots_top--;
}
