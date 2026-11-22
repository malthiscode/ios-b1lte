/*
 * B1L-0088: b2l-bicomp -- one AIR module through what the phone does with it, on the Mac:
 *   Scion's AIR front end (air_to_nir, every air-b2l patch), the I/O lowering libhakiosair's finalize_for_gallium
 *   does, bifrost_preprocess_nir and bifrost_compile_shader_nir for a Mali-G76 (gpu_id 0x7211), built with
 *   assertions ON (build-mesa/build-host-pan, buildtype debug) -- then the Bifrost disassembly of the binary.
 * Its purpose: find the shader whose code a Mali job stopped on with INSTR_INVALID_ENC (0x51), from the modules
 * backboardd sent through the log (scripts/extract-b2lmod.py).
 *   b2l-bicomp MODULE.air [--disasm] [--gpu 0xNNNN]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "compiler/air/nir_air.h"
#include "panfrost/compiler/bifrost_compile.h"
#include "panfrost/compiler/bifrost/disassemble.h"
#include "util/ralloc.h"
#include "util/u_dynarray.h"

static int
type_size_vec4(const struct glsl_type *type, bool bindless)
{
   (void)bindless;
   return glsl_count_attribute_slots(type, false);
}

/* What panfrost's Gallium lowering would give these, which this tool does not reproduce: zero, for an encoding check */
static bool
b2l_zero_gallium_sysvals(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   (void)data;
   switch (intr->intrinsic) {
   case nir_intrinsic_load_rt_conversion_pan:
   case nir_intrinsic_load_viewport_scale:
   case nir_intrinsic_load_viewport_offset:
   case nir_intrinsic_load_base_instance:
   case nir_intrinsic_load_base_vertex:
   case nir_intrinsic_load_first_vertex:
   case nir_intrinsic_load_ssbo_address:
   case nir_intrinsic_load_noperspective_varyings_pan:
      break;
   default:
      return false;
   }
   b->cursor = nir_before_instr(&intr->instr);
   /* B1L-0224: a value read from a constant buffer, as panfrost's sysval UBO gives it on the phone; a constant zero
    * folds a buffer's 64-bit address into a load_const Bifrost cannot emit (an assertion only this tool hits) */
   nir_def_replace(&intr->def, nir_load_ubo(b, intr->def.num_components, intr->def.bit_size, nir_imm_int(b, 0),
                                            nir_imm_int(b, 0), .align_mul = 4, .align_offset = 0,
                                            .range_base = 0, .range = ~0));
   return true;
}

/* B1L-0226: a texture size, which panfrost_nir_lower_sysvals turns into a sysval UBO read on the phone */
static bool
b2l_txs_as_ubo(nir_builder *b, nir_instr *instr, void *data)
{
   (void)data;
   if (instr->type != nir_instr_type_tex || nir_instr_as_tex(instr)->op != nir_texop_txs)
      return false;
   nir_tex_instr *tex = nir_instr_as_tex(instr);
   b->cursor = nir_before_instr(instr);
   nir_def_replace(&tex->def, nir_load_ubo(b, tex->def.num_components, tex->def.bit_size, nir_imm_int(b, 0),
                                           nir_imm_int(b, 16), .align_mul = 4, .align_offset = 0,
                                           .range_base = 0, .range = ~0));
   return true;
}

int
main(int argc, char **argv)
{
   const char *path = NULL, *fn = NULL;   /* fn: --fn NAME picks one function of a .metallib */
   bool disasm = false;
   unsigned gpu_id = 0x7211;
   const void *linked[8];   /* B1L-0234: --link MODULE.air, a module the pipeline links */
   size_t linked_size[8];
   unsigned nlinked = 0;
   for (int i = 1; i < argc; i++) {
      if (!strcmp(argv[i], "--disasm"))
         disasm = true;
      else if (!strcmp(argv[i], "--fn") && i + 1 < argc)
         fn = argv[++i];
      else if (!strcmp(argv[i], "--gpu") && i + 1 < argc)
         gpu_id = strtoul(argv[++i], NULL, 0);
      else if (!strcmp(argv[i], "--link") && i + 1 < argc && nlinked < 8) {
         FILE *lf = fopen(argv[++i], "rb");
         if (!lf) {
            perror(argv[i]);
            return 2;
         }
         fseek(lf, 0, SEEK_END);
         linked_size[nlinked] = ftell(lf);
         fseek(lf, 0, SEEK_SET);
         void *ld = malloc(linked_size[nlinked]);
         if (fread(ld, 1, linked_size[nlinked], lf) != linked_size[nlinked])
            return 2;
         fclose(lf);
         linked[nlinked++] = ld;
      }
      else
         path = argv[i];
   }
   if (!path) {
      fprintf(stderr, "usage: b2l-bicomp MODULE.air|LIB.metallib --fn NAME [--disasm] [--gpu 0xNNNN]\n");
      return 2;
   }
   FILE *f = fopen(path, "rb");
   if (!f) {
      perror(path);
      return 2;
   }
   fseek(f, 0, SEEK_END);
   long size = ftell(f);
   fseek(f, 0, SEEK_SET);
   void *data = malloc(size);
   if (fread(data, 1, size, f) != (size_t)size)
      return 2;
   fclose(f);

   glsl_type_singleton_init_or_ref();
   void *ctx = ralloc_context(NULL);
   if (fn && size >= 4 && !memcmp(data, "MTLB", 4)) {   /* B1L-0224: one function out of a .metallib */
      struct air_metallib lib;
      const char *lerr = NULL;
      bool found = false;
      if (!air_metallib_parse(ctx, data, size, &lib, &lerr)) {
         printf("%s: %s\n", path, lerr ? lerr : "not a metallib");
         return 2;
      }
      for (unsigned i = 0; i < lib.num_functions && !found; i++)
         if (!strcmp(lib.functions[i].name, fn)) {
            data = (void *)lib.functions[i].bitcode;
            size = lib.functions[i].bitcode_size;
            found = true;
         }
      if (!found) {
         printf("%s: no function %s\n", path, fn);
         return 2;
      }
      path = fn;
   }
   struct air_constant_value *fc;
   unsigned nfc = air_module_function_constants(ctx, data, size, &fc);
   struct air_to_nir_options opts = { .constants = fc, .num_constants = nfc,
                                      .b2l_linked = linked, .b2l_linked_size = linked_size,
                                      .b2l_num_linked = nlinked };
   struct air_shader_info info;
   const char *err = NULL;
   nir_shader *nir = air_to_nir(ctx, data, size, NULL, &opts, &bifrost_nir_options_v6, &info, &err);
   if (!nir) {
      printf("%s: translate FAILED: %s\n", path, err ? err : "?");
      return 1;
   }
   /* what libhakiosair's finalize_for_gallium does before Gallium sees the shader */
   NIR_PASS(_, nir, nir_lower_io, nir_var_shader_in | nir_var_shader_out, type_size_vec4,
            nir_lower_io_use_interpolated_input_intrinsics);
   NIR_PASS(_, nir, nir_lower_system_values);   /* Gallium's state tracker does it on the phone */
   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));
   bifrost_preprocess_nir(nir, gpu_id);

   NIR_PASS(_, nir, nir_shader_intrinsics_pass, b2l_zero_gallium_sysvals, nir_metadata_control_flow, NULL);
   NIR_PASS(_, nir, nir_shader_instructions_pass, b2l_txs_as_ubo, nir_metadata_control_flow, NULL);
   struct panfrost_compile_inputs inputs = { .gpu_id = gpu_id, .pushable_ubos = 1 };
   struct util_dynarray binary;
   util_dynarray_init(&binary, NULL);
   struct pan_shader_info pinfo;
   memset(&pinfo, 0, sizeof(pinfo));
   if (getenv("B2L_NIR"))   /* B1L-0224: the NIR Bifrost is handed */
      nir_print_shader(nir, stdout);
   bifrost_compile_shader_nir(nir, &inputs, &binary, &pinfo);
   printf("%s: %s, %u bytes of Bifrost code, %u work registers\n", path,
          gl_shader_stage_name(nir->info.stage), binary.size, pinfo.work_reg_count);
   if (disasm)
      disassemble_bifrost(stdout, binary.data, binary.size, false);
   util_dynarray_fini(&binary);
   ralloc_free(ctx);
   return 0;
}
