#include "../mir.h"

static int external_symbol;

int main (void) {
  char main_name[] = "main";
  MIR_context_t ctx = MIR_init ();
  MIR_module_t module = MIR_new_module (ctx, "global_item_test");
  MIR_item_t main_func;
  MIR_item_t global_main;
  MIR_item_t external;

  MIR_new_export (ctx, main_name);
  main_func = MIR_new_func (ctx, main_name, 0, NULL, 0);
  MIR_finish_func (ctx);
  MIR_finish_module (ctx);

  if (MIR_get_global_item (ctx, main_name) != NULL) return 1;
  MIR_load_module (ctx, module);
  global_main = MIR_get_global_item (ctx, main_name);
  if (global_main != main_func) return 1;
  if (MIR_get_item_func (ctx, global_main) != main_func->u.func) return 1;

  MIR_load_external (ctx, "external_symbol", &external_symbol);
  external = MIR_get_global_item (ctx, "external_symbol");
  if (external == NULL || external->item_type != MIR_import_item) return 1;
  if (external->addr != &external_symbol) return 1;

  if (MIR_get_global_item (ctx, "missing") != NULL) return 1;
  if (MIR_get_global_item (ctx, NULL) != NULL) return 1;

  MIR_finish (ctx);
  return 0;
}
