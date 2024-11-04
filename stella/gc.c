#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <assert.h>

#include "runtime.h"
#include "gc.h"

typedef struct gc_object gc_object;
struct gc_object {
    void *new_ptr;
    stella_object obj;
};

struct gc_space {
    size_t  size;
    void *start;
    void *next;
} g0_from, g1_from, g1_to;

struct gc_gen_descriptor {
    int idx;

    void *scan;

    struct gc_space *from;
    struct gc_space *to;
} gc_gen0, gc_gen1;

bool has_enough_space(const struct gc_space* space, const size_t requested_size) {
    return space->next + requested_size <= space->start + space->size;
}

#define GC_GEN_COUNT 2

#define MAX_GC_ROOTS 1024
#define MAX_ALLOC_SIZE (16 * 100)

// for debug and testing
//#define DISABLE_GC

/** GC statistics **/
size_t total_allocated_bytes = 0;
int total_allocated_objects = 0;

size_t max_allocated_bytes = 0;
size_t max_allocated_objects = 0;

long gc_runs_total = 0;

int total_reads = 0;
int total_writes = 0;

int gc_roots_max_size = 0;
int gc_roots_top = 0;
void **gc_roots[MAX_GC_ROOTS];
int gc_roots_in_other_gens_top = 0;
void **gc_roots_in_other_gens[MAX_GC_ROOTS];

struct gc_gen_descriptor* gc_generations[GC_GEN_COUNT] = { &gc_gen0, &gc_gen1 };
bool was_heap_allocated = false;

void* gc_forward(struct gc_gen_descriptor* gen, gc_object *ptr);
bool gc_chase(struct gc_gen_descriptor* gen, gc_object *ptr);
void gc_collect(struct gc_gen_descriptor* gen);
void gc_collect_all();
void gc_clean_space(struct gc_space*);
size_t gc_obj_size(gc_object*);
bool gc_is_pointer_in_space(struct gc_space*, void*);
void init_generations();
void* try_alloc(struct gc_gen_descriptor*, size_t);
struct gc_object* try_alloc_in_space(struct gc_space*, size_t);

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
    if (!was_heap_allocated) {
        was_heap_allocated = true;
        init_generations();
    }

  total_allocated_bytes += size_in_bytes;
  total_allocated_objects += 1;
  max_allocated_bytes = total_allocated_bytes;
  max_allocated_objects = total_allocated_objects;

  gc_object* allocated = try_alloc(gc_generations[0], size_in_bytes);
  if (allocated == NULL) {
      gc_collect_all();
      allocated = try_alloc(gc_generations[0], size_in_bytes);
  }

  if (allocated == NULL) {
      printf("Out of memory");
      exit(137);
  }

  return allocated;
}

#endif

void init_generations() {
    size_t arena_size = MAX_ALLOC_SIZE * (GC_GEN_COUNT + 1); // +1 is because of last gen is being doubled
    void *arena = malloc(arena_size);

    g0_from.start = arena;
    g0_from.next = g0_from.start;
    g0_from.size = MAX_ALLOC_SIZE;

    g1_from.start = arena + MAX_ALLOC_SIZE;
    g1_from.next = g1_from.start;
    g1_from.size = MAX_ALLOC_SIZE;

    struct gc_space *g0_to = &g1_from;

    g1_to.start = g1_from.start + MAX_ALLOC_SIZE;
    g1_to.next = g1_to.start;
    g1_to.size = MAX_ALLOC_SIZE;

    gc_generations[0]->from = &g0_from;
    gc_generations[0]->to = g0_to;
    gc_generations[0]->idx = 0;

    gc_generations[1]->from = &g1_from;
    gc_generations[1]->to = &g1_to;
    gc_generations[1]->idx = 1;
}

struct gc_object* try_alloc_in_space(struct gc_space* space, size_t size_bytes) {
    size_t size_with_wrapper = size_bytes + sizeof (void *);
    if (!has_enough_space(space, size_with_wrapper)) {
        return NULL;
    }

    struct gc_object *result = space->next;
    result->new_ptr = NULL;
    result->obj.object_header = 0;
    space->next += size_with_wrapper;

    return result;
}

void gc_collect_all() {
    gc_collect(gc_generations[0]);
}

// todo: check for OOM here
void* gc_forward(struct gc_gen_descriptor* gen, gc_object *ptr) {
    if (gc_is_pointer_in_space(gen->from, ptr)) {
        void *possibleNewObjectAddress = ptr->new_ptr;
        if (gc_is_pointer_in_space(gen->to, possibleNewObjectAddress)) {
            return possibleNewObjectAddress;
        } else {
            bool result = gc_chase(gen, ptr);
            if (!result) {
              gc_collect(gc_generations[1]);
              result = gc_chase(gen, ptr);
            }

            if (!result) {
              printf("out of memory");
              exit(137);
            }

//            assert(gc_is_pointer_in_space(gen->to, ptr->new_ptr));
            return ptr->new_ptr;
        }
    }

    return ptr;
}

bool gc_chase(struct gc_gen_descriptor* gen, gc_object *ptr) {
    do {
        struct gc_object *q = try_alloc_in_space(gen->to, gc_obj_size(ptr));
        if (q == NULL) {
          return false;
        }

        int fields_count = STELLA_OBJECT_HEADER_FIELD_COUNT(ptr->obj.object_header);
        void *r = NULL;

        q->obj.object_header = ptr->obj.object_header;
        for (int i = 0; i < fields_count; i++) {
          q->obj.object_fields[i] = ptr->obj.object_fields[i];

            if (gc_is_pointer_in_space(gen->from, q->obj.object_fields[i])) {
              stella_object *potentially_forwarded = q->new_ptr;

              if (!gc_is_pointer_in_space(gen->to, potentially_forwarded)) {
                r = potentially_forwarded;
              }
            }
        }

        ptr->new_ptr = q;
        ptr = r;
    } while(ptr != NULL);

    return true;
}

void gc_collect(struct gc_gen_descriptor* gen) {
    gc_runs_total++;

    gen->scan = gen->to->next;

    for (int root_i = 0; root_i < gc_roots_top; root_i++) {
        void **root_ptr = gc_roots[root_i];
        *root_ptr = gc_forward(gen, *root_ptr);
    }

    for (int root_i = 0; root_i < gc_roots_in_other_gens_top; root_i++) {
        void **root_ptr = gc_roots_in_other_gens[root_i];
        *root_ptr = gc_forward(gen, *root_ptr);
    }

    for (int i = 0; i < gen->idx; i++) {
      struct gc_gen_descriptor* prev_gen = gc_generations[i];

      for (void *ptr = prev_gen->from->start; ptr < prev_gen->from->next; ptr += gc_obj_size(ptr)) {
        gc_object *gc_obj = ptr;
        int fields_count = STELLA_OBJECT_HEADER_FIELD_COUNT(gc_obj->obj.object_header);
        for (int field_i = 0; field_i < fields_count; field_i++) {
          gc_obj->obj.object_fields[field_i] = gc_forward(gen, gc_obj->obj.object_fields[i]);
        }
      }
    }

    while (gen->scan < gen->to->next) {
        gc_object *gc_o = gen->scan;
        stella_object *obj = &gc_o->obj;
        int fields_count = STELLA_OBJECT_HEADER_FIELD_COUNT(obj->object_header);
        for (int field_i = 0; field_i < fields_count; field_i++) {
            obj->object_fields[field_i] = gc_forward(gen, obj->object_fields[field_i]);
        }

      gen->scan += gc_obj_size(gen->scan);
    }

  gc_clean_space(gen->from); // todo: debug only
}

void gc_clean_space(struct gc_space* space) {
    for (void* i = space->start; i < (void*)space->start + space->size; i++) {
        *(char *)i = 0;
    }
}

void *try_alloc(struct gc_gen_descriptor* g, size_t size_bytes) {
    gc_object *allocated = try_alloc_in_space(g->from, size_bytes);
    if (allocated == NULL) {
        return NULL;
    }

    return &allocated->obj;
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
//      printf(" allocated %zu/%d\n", (gc_generations_next[i] - gc_generations[i]), max);
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

size_t gc_obj_size(gc_object *obj) {
  int fields_count = STELLA_OBJECT_HEADER_FIELD_COUNT(obj->obj.object_header);

  return (2 + fields_count) * sizeof (void*);
}

bool gc_is_pointer_in_space(struct gc_space* space, void* ptr) {
  void *startPtr = (void *) space->start;
  return startPtr <= ptr && ptr < (startPtr + space->size);
}
