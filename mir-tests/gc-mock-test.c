#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../mir-gen.h"

#define HEAP_SIZE 64
#define MAX_SAVED_VISITS 8

struct obj {
  int alive;
  int marked;
};

static struct obj heap[HEAP_SIZE];
static MIR_context_t current_ctx;
static MIR_item_t current_func;
static size_t last_visit_count, collect_count;
static size_t saved_visit_counts[MAX_SAVED_VISITS];
static uintptr_t last_alloc_handle;

static void fail (const char *msg) {
  fprintf (stderr, "gc-mock-test: %s\n", msg);
  exit (1);
}

static void check (int cond, const char *msg) {
  if (!cond) fail (msg);
}

static void reset_heap (void) {
  memset (heap, 0, sizeof (heap));
  last_visit_count = collect_count = 0;
  memset (saved_visit_counts, 0, sizeof (saved_visit_counts));
  last_alloc_handle = 0;
}

static uintptr_t handle_from_index (size_t index) { return ((uintptr_t) (index + 1) << 1) | 1; }

static int handle_index (uintptr_t handle, size_t *index) {
  if ((handle & 1) == 0) return 0;
  handle >>= 1;
  if (handle == 0 || handle > HEAP_SIZE) return 0;
  *index = (size_t) handle - 1;
  return 1;
}

static int alive_handle (uintptr_t handle) {
  size_t index;

  return handle_index (handle, &index) && heap[index].alive;
}

uintptr_t mock_alloc_gc (int64_t id) {
  size_t index = (size_t) id % HEAP_SIZE;

  heap[index].alive = 1;
  heap[index].marked = 0;
  last_alloc_handle = handle_from_index (index);
  return last_alloc_handle;
}

uintptr_t mock_alloc_i64 (int64_t id) { return mock_alloc_gc (id); }

static void mark_root (void *root_addr, void *data MIR_UNUSED) {
  uintptr_t handle = *(uintptr_t *) root_addr;
  size_t index;

  if (handle_index (handle, &index) && heap[index].alive) heap[index].marked = 1;
}

void mock_gc_collect_at_safepoint (void) {
  void *return_pc = __builtin_return_address (0);
  void *frame_base = __builtin_frame_address (1);

  for (size_t i = 0; i < HEAP_SIZE; i++) heap[i].marked = 0;
  last_visit_count = MIR_visit_gc_roots (current_ctx, current_func, return_pc, frame_base,
                                         mark_root, NULL);
  if (collect_count < MAX_SAVED_VISITS) saved_visit_counts[collect_count] = last_visit_count;
  collect_count++;
  for (size_t i = 0; i < HEAP_SIZE; i++)
    if (heap[i].alive && !heap[i].marked) heap[i].alive = 0;
}

void mock_gc_collect_arg_at_safepoint (uintptr_t arg MIR_UNUSED) {
  void *return_pc = __builtin_return_address (0);
  void *frame_base = __builtin_frame_address (1);

  for (size_t i = 0; i < HEAP_SIZE; i++) heap[i].marked = 0;
  last_visit_count = MIR_visit_gc_roots (current_ctx, current_func, return_pc, frame_base,
                                         mark_root, NULL);
  if (collect_count < MAX_SAVED_VISITS) saved_visit_counts[collect_count] = last_visit_count;
  collect_count++;
  for (size_t i = 0; i < HEAP_SIZE; i++)
    if (heap[i].alive && !heap[i].marked) heap[i].alive = 0;
}

static MIR_item_t new_import (MIR_context_t ctx, const char *name) {
  return MIR_new_import (ctx, name);
}

static MIR_item_t new_alloc_proto (MIR_context_t ctx, const char *name, MIR_type_t res_type) {
  return MIR_new_proto (ctx, name, 1, &res_type, 1, MIR_T_I64, "id");
}

static MIR_item_t new_collect_proto (MIR_context_t ctx) {
  return MIR_new_proto (ctx, "p_collect", 0, NULL, 0);
}

static MIR_item_t new_collect_arg_proto (MIR_context_t ctx) {
  return MIR_new_proto (ctx, "p_collect_arg", 0, NULL, 1, MIR_T_GC, "arg");
}

static MIR_insn_t append_alloc_call (MIR_context_t ctx, MIR_item_t func, MIR_item_t proto,
                                     MIR_item_t import, MIR_reg_t result, int64_t id) {
  MIR_insn_t call
    = MIR_new_call_insn (ctx, 4, MIR_new_ref_op (ctx, proto), MIR_new_ref_op (ctx, import),
                         MIR_new_reg_op (ctx, result), MIR_new_int_op (ctx, id));
  MIR_append_insn (ctx, func, call);
  return call;
}

static MIR_insn_t append_collect_call (MIR_context_t ctx, MIR_item_t func, MIR_item_t proto,
                                       MIR_item_t import) {
  MIR_insn_t call
    = MIR_new_call_insn (ctx, 2, MIR_new_ref_op (ctx, proto), MIR_new_ref_op (ctx, import));
  MIR_append_insn (ctx, func, call);
  return call;
}

static MIR_insn_t append_collect_arg_call (MIR_context_t ctx, MIR_item_t func, MIR_item_t proto,
                                           MIR_item_t import, MIR_reg_t arg) {
  MIR_insn_t call
    = MIR_new_call_insn (ctx, 3, MIR_new_ref_op (ctx, proto), MIR_new_ref_op (ctx, import),
                         MIR_new_reg_op (ctx, arg));
  MIR_append_insn (ctx, func, call);
  return call;
}

static void finish_and_load (MIR_context_t ctx, MIR_module_t module) {
  MIR_finish_func (ctx);
  MIR_finish_module (ctx);
  MIR_load_module (ctx, module);
  MIR_load_external (ctx, "mock_alloc_gc", mock_alloc_gc);
  MIR_load_external (ctx, "mock_alloc_i64", mock_alloc_i64);
  MIR_load_external (ctx, "mock_gc_collect_at_safepoint", mock_gc_collect_at_safepoint);
  MIR_load_external (ctx, "mock_gc_collect_arg_at_safepoint", mock_gc_collect_arg_at_safepoint);
}

static void check_safepoints (MIR_context_t ctx, MIR_item_t func, const char *name,
                              unsigned level, size_t expected_sps, const size_t *expected_roots) {
  size_t count;
  const MIR_gc_safepoint_t *sps = MIR_get_gc_safepoints (ctx, func, &count);

  if (count != expected_sps) {
    fprintf (stderr, "gc-mock-test: %s at O%u: expected %lu safepoints, got %lu\n", name, level,
             (unsigned long) expected_sps, (unsigned long) count);
    fail ("unexpected safepoint count");
  }
  if (count == 0) {
    check (sps == NULL, "empty safepoint table should be NULL");
    return;
  }
  check (sps != NULL, "missing safepoint table");
  for (size_t i = 0; i < count; i++) {
    check (sps[i].code_offset != (size_t) -1, "missing call code offset");
    check (sps[i].return_pc_offset != (size_t) -1, "missing return pc offset");
    check (sps[i].nroots == expected_roots[i], "unexpected safepoint root count");
  }
}

static MIR_item_t make_auto_live_func (MIR_context_t ctx) {
  MIR_type_t gc_type = MIR_T_GC;
  MIR_module_t m = MIR_new_module (ctx, "gc_auto_m");
  MIR_item_t alloc_proto = new_alloc_proto (ctx, "p_alloc_gc", MIR_T_GC);
  MIR_item_t collect_proto = new_collect_proto (ctx);
  MIR_item_t alloc_import = new_import (ctx, "mock_alloc_gc");
  MIR_item_t collect_import = new_import (ctx, "mock_gc_collect_at_safepoint");
  MIR_item_t func = MIR_new_func (ctx, "gc_auto_live", 1, &gc_type, 0);
  MIR_reg_t obj = MIR_new_func_reg (ctx, func->u.func, MIR_T_GC, "obj");

  append_alloc_call (ctx, func, alloc_proto, alloc_import, obj, 0);
  append_collect_call (ctx, func, collect_proto, collect_import);
  MIR_append_insn (ctx, func, MIR_new_ret_insn (ctx, 1, MIR_new_reg_op (ctx, obj)));
  finish_and_load (ctx, m);
  return func;
}

static MIR_item_t make_pressure_func (MIR_context_t ctx) {
  MIR_type_t res_type = MIR_T_I64;
  MIR_module_t m = MIR_new_module (ctx, "gc_pressure_m");
  MIR_item_t alloc_proto = new_alloc_proto (ctx, "p_alloc_gc", MIR_T_GC);
  MIR_item_t collect_proto = new_collect_proto (ctx);
  MIR_item_t alloc_import = new_import (ctx, "mock_alloc_gc");
  MIR_item_t collect_import = new_import (ctx, "mock_gc_collect_at_safepoint");
  MIR_item_t func = MIR_new_func (ctx, "gc_pressure", 1, &res_type, 0);
  MIR_reg_t obj = MIR_new_func_reg (ctx, func->u.func, MIR_T_GC, "obj");
  MIR_reg_t raw = MIR_new_func_reg (ctx, func->u.func, MIR_T_I64, "raw");
  MIR_reg_t sum = MIR_new_func_reg (ctx, func->u.func, MIR_T_I64, "sum");
  MIR_reg_t regs[32];

  append_alloc_call (ctx, func, alloc_proto, alloc_import, obj, 3);
  for (int i = 0; i < 32; i++) {
    char name[16];
    MIR_reg_t reg;

    snprintf (name, sizeof (name), "r%d", i);
    reg = MIR_new_func_reg (ctx, func->u.func, MIR_T_I64, name);
    MIR_append_insn (ctx, func,
                     MIR_new_insn (ctx, MIR_MOV, MIR_new_reg_op (ctx, reg),
                                   MIR_new_int_op (ctx, 1000 + i)));
    regs[i] = reg;
  }
  append_collect_call (ctx, func, collect_proto, collect_import);
  MIR_append_insn (ctx, func,
                   MIR_new_insn (ctx, MIR_MOV, MIR_new_reg_op (ctx, raw),
                                 MIR_new_reg_op (ctx, obj)));
  MIR_append_insn (ctx, func,
                   MIR_new_insn (ctx, MIR_MOV, MIR_new_reg_op (ctx, sum),
                                 MIR_new_reg_op (ctx, raw)));
  for (int i = 0; i < 32; i++)
    MIR_append_insn (ctx, func,
                     MIR_new_insn (ctx, MIR_ADD, MIR_new_reg_op (ctx, sum),
                                   MIR_new_reg_op (ctx, sum), MIR_new_reg_op (ctx, regs[i])));
  MIR_append_insn (ctx, func, MIR_new_ret_insn (ctx, 1, MIR_new_reg_op (ctx, sum)));
  finish_and_load (ctx, m);
  return func;
}

static MIR_item_t make_explicit_mem_func (MIR_context_t ctx) {
  MIR_type_t res_type = MIR_T_I64;
  MIR_module_t m = MIR_new_module (ctx, "gc_mem_m");
  MIR_item_t alloc_proto = new_alloc_proto (ctx, "p_alloc_gc", MIR_T_GC);
  MIR_item_t collect_proto = new_collect_proto (ctx);
  MIR_item_t alloc_import = new_import (ctx, "mock_alloc_gc");
  MIR_item_t collect_import = new_import (ctx, "mock_gc_collect_at_safepoint");
  MIR_item_t func = MIR_new_func (ctx, "gc_explicit_mem", 1, &res_type, 0);
  MIR_reg_t obj1 = MIR_new_func_reg (ctx, func->u.func, MIR_T_GC, "obj1");
  MIR_reg_t obj2 = MIR_new_func_reg (ctx, func->u.func, MIR_T_GC, "obj2");
  MIR_reg_t buf = MIR_new_func_reg (ctx, func->u.func, MIR_T_I64, "buf");
  MIR_insn_t collect_call;

  MIR_append_insn (ctx, func,
                   MIR_new_insn (ctx, MIR_ALLOCA, MIR_new_reg_op (ctx, buf), MIR_new_int_op (ctx, 16)));
  append_alloc_call (ctx, func, alloc_proto, alloc_import, obj1, 4);
  append_alloc_call (ctx, func, alloc_proto, alloc_import, obj2, 5);
  MIR_append_insn (ctx, func,
                   MIR_new_insn (ctx, MIR_MOV, MIR_new_mem_op (ctx, MIR_T_GC, 0, buf, 0, 1),
                                 MIR_new_reg_op (ctx, obj1)));
  MIR_append_insn (ctx, func,
                   MIR_new_insn (ctx, MIR_MOV, MIR_new_mem_op (ctx, MIR_T_GC, 8, buf, 0, 1),
                                 MIR_new_reg_op (ctx, obj2)));
  collect_call = append_collect_call (ctx, func, collect_proto, collect_import);
  MIR_add_call_gc_root_mem (ctx, collect_call, MIR_T_GC, buf, 0, 2, 8);
  MIR_append_insn (ctx, func, MIR_new_ret_insn (ctx, 1, MIR_new_int_op (ctx, 0)));
  finish_and_load (ctx, m);
  return func;
}

static MIR_item_t make_call_arg_func (MIR_context_t ctx) {
  MIR_type_t res_type = MIR_T_I64;
  MIR_module_t m = MIR_new_module (ctx, "gc_call_arg_m");
  MIR_item_t alloc_proto = new_alloc_proto (ctx, "p_alloc_gc", MIR_T_GC);
  MIR_item_t collect_proto = new_collect_arg_proto (ctx);
  MIR_item_t alloc_import = new_import (ctx, "mock_alloc_gc");
  MIR_item_t collect_import = new_import (ctx, "mock_gc_collect_arg_at_safepoint");
  MIR_item_t func = MIR_new_func (ctx, "gc_call_arg", 1, &res_type, 0);
  MIR_reg_t obj = MIR_new_func_reg (ctx, func->u.func, MIR_T_GC, "obj");

  append_alloc_call (ctx, func, alloc_proto, alloc_import, obj, 10);
  append_collect_arg_call (ctx, func, collect_proto, collect_import, obj);
  MIR_append_insn (ctx, func, MIR_new_ret_insn (ctx, 1, MIR_new_int_op (ctx, 0)));
  finish_and_load (ctx, m);
  return func;
}

static MIR_item_t make_explicit_reg_func (MIR_context_t ctx) {
  MIR_type_t res_type = MIR_T_I64;
  MIR_module_t m = MIR_new_module (ctx, "gc_reg_m");
  MIR_item_t alloc_proto = new_alloc_proto (ctx, "p_alloc_gc", MIR_T_GC);
  MIR_item_t collect_proto = new_collect_proto (ctx);
  MIR_item_t alloc_import = new_import (ctx, "mock_alloc_gc");
  MIR_item_t collect_import = new_import (ctx, "mock_gc_collect_at_safepoint");
  MIR_item_t func = MIR_new_func (ctx, "gc_explicit_reg", 1, &res_type, 0);
  MIR_reg_t obj = MIR_new_func_reg (ctx, func->u.func, MIR_T_GC, "obj");
  MIR_insn_t collect_call;

  append_alloc_call (ctx, func, alloc_proto, alloc_import, obj, 9);
  collect_call = append_collect_call (ctx, func, collect_proto, collect_import);
  MIR_add_call_gc_root_reg (ctx, collect_call, obj);
  MIR_append_insn (ctx, func, MIR_new_ret_insn (ctx, 1, MIR_new_int_op (ctx, 0)));
  finish_and_load (ctx, m);
  return func;
}

static MIR_item_t make_non_gc_func (MIR_context_t ctx) {
  MIR_type_t res_type = MIR_T_I64;
  MIR_module_t m = MIR_new_module (ctx, "gc_non_gc_m");
  MIR_item_t alloc_proto = new_alloc_proto (ctx, "p_alloc_i64", MIR_T_I64);
  MIR_item_t collect_proto = new_collect_proto (ctx);
  MIR_item_t alloc_import = new_import (ctx, "mock_alloc_i64");
  MIR_item_t collect_import = new_import (ctx, "mock_gc_collect_at_safepoint");
  MIR_item_t func = MIR_new_func (ctx, "gc_non_gc", 1, &res_type, 0);
  MIR_reg_t raw = MIR_new_func_reg (ctx, func->u.func, MIR_T_I64, "raw");

  append_alloc_call (ctx, func, alloc_proto, alloc_import, raw, 6);
  append_collect_call (ctx, func, collect_proto, collect_import);
  MIR_append_insn (ctx, func, MIR_new_ret_insn (ctx, 1, MIR_new_reg_op (ctx, raw)));
  finish_and_load (ctx, m);
  return func;
}

static MIR_item_t make_multi_func (MIR_context_t ctx) {
  MIR_type_t res_type = MIR_T_I64;
  MIR_module_t m = MIR_new_module (ctx, "gc_multi_m");
  MIR_item_t alloc_proto = new_alloc_proto (ctx, "p_alloc_gc", MIR_T_GC);
  MIR_item_t collect_proto = new_collect_proto (ctx);
  MIR_item_t alloc_import = new_import (ctx, "mock_alloc_gc");
  MIR_item_t collect_import = new_import (ctx, "mock_gc_collect_at_safepoint");
  MIR_item_t func = MIR_new_func (ctx, "gc_multi", 1, &res_type, 0);
  MIR_reg_t obj1 = MIR_new_func_reg (ctx, func->u.func, MIR_T_GC, "obj1");
  MIR_reg_t obj2 = MIR_new_func_reg (ctx, func->u.func, MIR_T_GC, "obj2");
  MIR_reg_t raw1 = MIR_new_func_reg (ctx, func->u.func, MIR_T_I64, "raw1");
  MIR_reg_t raw2 = MIR_new_func_reg (ctx, func->u.func, MIR_T_I64, "raw2");
  MIR_reg_t sum = MIR_new_func_reg (ctx, func->u.func, MIR_T_I64, "sum");

  append_alloc_call (ctx, func, alloc_proto, alloc_import, obj1, 7);
  append_collect_call (ctx, func, collect_proto, collect_import);
  append_alloc_call (ctx, func, alloc_proto, alloc_import, obj2, 8);
  append_collect_call (ctx, func, collect_proto, collect_import);
  MIR_append_insn (ctx, func,
                   MIR_new_insn (ctx, MIR_MOV, MIR_new_reg_op (ctx, raw1),
                                 MIR_new_reg_op (ctx, obj1)));
  MIR_append_insn (ctx, func,
                   MIR_new_insn (ctx, MIR_MOV, MIR_new_reg_op (ctx, raw2),
                                 MIR_new_reg_op (ctx, obj2)));
  MIR_append_insn (ctx, func,
                   MIR_new_insn (ctx, MIR_ADD, MIR_new_reg_op (ctx, sum),
                                 MIR_new_reg_op (ctx, raw1), MIR_new_reg_op (ctx, raw2)));
  MIR_append_insn (ctx, func, MIR_new_ret_insn (ctx, 1, MIR_new_reg_op (ctx, sum)));
  finish_and_load (ctx, m);
  return func;
}

static void run_case (unsigned level, const char *name, MIR_item_t (*make_func) (MIR_context_t),
                      size_t expected_sps, const size_t *expected_roots, const int *alive_ids,
                      size_t alive_ids_num, size_t expected_collects, int expect_last_dead) {
  MIR_context_t ctx = MIR_init ();
  MIR_item_t func = make_func (ctx);
  uintptr_t (*call_func) (void);

  reset_heap ();
  current_ctx = ctx;
  current_func = func;
  MIR_gen_init (ctx);
  MIR_gen_set_optimize_level (ctx, level);
  MIR_link (ctx, MIR_set_gen_interface, NULL);
  call_func = (uintptr_t (*) (void)) MIR_gen (ctx, func);
  (void) call_func ();
  check_safepoints (ctx, func, name, level, expected_sps, expected_roots);
  check (collect_count == expected_collects, "unexpected collection count");
  for (size_t i = 0; i < alive_ids_num; i++)
    check (alive_handle (handle_from_index ((size_t) alive_ids[i] % HEAP_SIZE)), name);
  if (expect_last_dead)
    check (!alive_handle (last_alloc_handle), name);
  MIR_gen_finish (ctx);
  MIR_finish (ctx);
  current_ctx = NULL;
  current_func = NULL;
}

int main (void) {
  const size_t one_root[] = {1};
  const size_t mem_roots[] = {1, 2};
  const size_t multi_roots[] = {1, 1, 2};
  const int auto_alive[] = {0};
  const int pressure_alive[] = {3};
  const int mem_alive[] = {4, 5};
  const int multi_alive[] = {7, 8};
  const int reg_alive[] = {9};
  const int call_arg_alive[] = {10};

  for (unsigned level = 0; level <= 3; level++) {
    run_case (level, "auto GC root was not preserved", make_auto_live_func, 1, one_root,
              auto_alive, 1, 1, 0);
    run_case (level, "high-pressure GC root was not preserved", make_pressure_func, 1, one_root,
              pressure_alive, 1, 1, 0);
    run_case (level, "explicit memory roots were not preserved", make_explicit_mem_func, 2,
              mem_roots, mem_alive, 2, 1, 0);
    run_case (level, "dead-after-call GC argument was not preserved", make_call_arg_func, 1,
              one_root, call_arg_alive, 1, 1, 0);
    run_case (level, "explicit register root was not preserved", make_explicit_reg_func, 1,
              one_root, reg_alive, 1, 1, 0);
    run_case (level, "non-GC integer was unexpectedly preserved", make_non_gc_func, 0, NULL, NULL,
              0, 1, 1);
    run_case (level, "multiple safepoints were not preserved", make_multi_func, 3, multi_roots,
              multi_alive, 2, 2, 0);
  }
  return 0;
}
