#!/usr/bin/env python3
# B1L-0058: our changes to Scion's AIR front end (tools/mesa/air, MIT), applied by scripts/build-mesa.sh after it copies
# the directory into Mesa. Each change checks the exact text it replaces and fails loudly if Scion's file moved.
import sys
p = sys.argv[1] + '/air_metal.c'
s = open(p).read()
a = '''   NIR_PASS(_, nir, nir_lower_io, nir_var_shader_in | nir_var_shader_out,
            type_size_vec4, nir_lower_io_use_interpolated_input_intrinsics);
}'''
b = '''   NIR_PASS(_, nir, nir_lower_io, nir_var_shader_in | nir_var_shader_out,
            type_size_vec4, nir_lower_io_use_interpolated_input_intrinsics);
   /*
    * B2L (B1L-0058): what the shader reads and writes, gathered from the lowered intrinsics. ir3 gathers this itself and
    * nir_to_tgsi does not need it, so nothing here ever did -- but panfrost decides from nir->info alone which render
    * targets a fragment shader writes (outputs_written), which attributes a vertex shader fetches (inputs_read) and which
    * varyings link the two (fixed_varying_mask, pan_shader.c). Without it, on the Mali, a draw wrote no colour (every
    * pixel the clear's 0, B1L-0056/57) and the vertex stage fetched nothing (every primitive on one pixel).
    */
   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));
}'''
if s.count(a) != 1:
    sys.exit('air-b2l: finalize_for_gallium is not as expected')
s = s.replace(a, b)

# B1L-0062: on panfrost (the only screen with ubo_base != 0) every Metal constant buffer goes to the driver as a user
# buffer, uploaded by panfrost at each draw, instead of the GPU address of the long-lived mapping. On the Mali the shaders
# read zeros from constant buffers bound by address (Apple's clearFragment20 wrote 0, std_vert0_lpf's matrix gave one
# pixel, B1L-0056..0061); softpipe read them right. A per-draw copy of a few hundred bytes is also what panfrost needs to
# push UBO words into a shader's uniforms ("only user buffers use this path", pan_cmdstream.c). The first few bindings
# are logged with their first words, to see what the shader is given.
a = """         if (boffset % device->ubo_offset_align == 0) {
            cb.buffer = b->res;
         } else {"""
b = """         if (boffset % device->ubo_offset_align == 0 && !device->ubo_base) {
            cb.buffer = b->res;
         } else {"""
if s.count(a) != 1:
    sys.exit('air-b2l: the constant buffer binding is not as expected')
s = s.replace(a, b)
a = """         pipe->set_constant_buffer(pipe, stage, r->index + device->ubo_base, false, &cb);"""
b = """         {
            static unsigned b2l_said;
            if (b2l_said < 12) {
               const uint32_t *w = (const uint32_t *)((const char *)b->map + boffset);
               b2l_said++;
               fprintf(stderr, "air-b2l: stage %u constant buffer %u (slot %u): %u bytes from offset %zu, %s; "
                       "words %08x %08x %08x %08x\\n", (unsigned)stage, r->index, r->index + device->ubo_base,
                       cb.buffer_size, boffset, cb.buffer ? "by address" : "uploaded by the driver",
                       w[0], w[1], w[2], w[3]);
            }
         }
         pipe->set_constant_buffer(pipe, stage, r->index + device->ubo_base, false, &cb);"""
if s.count(a) != 1:
    sys.exit('air-b2l: set_constant_buffer is not as expected')
s = s.replace(a, b)
open(p, 'w').write(s)
print('air-b2l: constant buffers uploaded by the driver on panfrost, first bindings logged')

# B1L-0063: panfrost pushes UBO words into a shader's uniforms (FAU) only for UBO 0 (GLES's default block) and the sysval
# UBO; every other constant buffer is read with LOAD.ubo -- a path the GLES window server never took on this phone, and
# which returns zeros there (B1L-0062: the right words handed to panfrost, the shaders still saw 0). Every UBO is made
# pushable; constant buffers are user buffers on panfrost since B1L-0062, which is what the push path reads. Loads the
# compiler cannot push (a non-constant offset, past the push limit) still go through LOAD.ubo.
q = sys.argv[1] + '/../../gallium/drivers/panfrost/pan_shader.c'
t = open(q).read()
a = """   inputs.pushable_ubos = BITFIELD_BIT(0);"""
b = """   /* B2L (B1L-0063): every UBO in a process that asks (libhakiosair's Metal clients set B2L_PAN_PUSH_ALL; their
    * constant buffers are user buffers). GLES keeps Mesa's rule: its other UBOs are real buffers, not CPU pointers. */
   inputs.pushable_ubos = getenv("B2L_PAN_PUSH_ALL") ? ~0u : BITFIELD_BIT(0);"""
if t.count(b) == 1:
    pass                                   # (Mesa's tree is kept between builds: already done)
elif t.count(a) != 1:
    sys.exit('air-b2l: pan_shader.c pushable_ubos is not as expected')
else:
    open(q, 'w').write(t.replace(a, b))
print('air-b2l: panfrost pushes every UBO')

# B1L-0066: panfrost does not transform positions in hardware: its compiler adds the viewport transform to the vertex
# shader (bifrost_compile.c -> nir_lower_viewport_transform), and that pass only looks for store_deref of the
# VARYING_SLOT_POS variable. libhakiosair hands panfrost shaders whose I/O is already lowered to store_output
# (finalize_for_gallium), so the pass found nothing and clip coordinates were written as window coordinates: every
# primitive landed on pixel (0,0) (B1L-0056..0065). The pass now also takes a store_output of VARYING_SLOT_POS.
r = sys.argv[1] + '/../nir/nir_lower_viewport_transform.c'
u = open(r).read()
a = """   if (intr->intrinsic != nir_intrinsic_store_deref)
      return false;

   nir_variable *var = nir_intrinsic_get_var(intr, 0);
   if (var->data.mode != nir_var_shader_out ||
       var->data.location != VARYING_SLOT_POS)
      return false;

   b->cursor = nir_before_instr(&intr->instr);

   /* Grab the source and viewport */
   nir_def *input_point = intr->src[1].ssa;"""
b = """   /* B2L (B1L-0066): a store_output of the position as well, for shaders whose I/O was lowered before the driver saw
    * them (libhakiosair's Metal shaders); only a whole-vec4 write from component 0 is transformed. */
   unsigned value_src;
   if (intr->intrinsic == nir_intrinsic_store_deref) {
      nir_variable *var = nir_intrinsic_get_var(intr, 0);
      if (var->data.mode != nir_var_shader_out ||
          var->data.location != VARYING_SLOT_POS)
         return false;
      value_src = 1;
   } else if (intr->intrinsic == nir_intrinsic_store_output) {
      if (nir_intrinsic_io_semantics(intr).location != VARYING_SLOT_POS ||
          nir_intrinsic_component(intr) != 0 ||
          nir_intrinsic_write_mask(intr) != 0xf ||
          intr->src[0].ssa->num_components != 4)
         return false;
      value_src = 0;
   } else {
      return false;
   }

   b->cursor = nir_before_instr(&intr->instr);

   /* Grab the source and viewport */
   nir_def *input_point = intr->src[value_src].ssa;"""
c_old = """   nir_src_rewrite(&intr->src[1], screen_space);"""
c_new = """   nir_src_rewrite(&intr->src[value_src], screen_space);"""
if u.count('B1L-0066') == 0:
    if u.count(a) != 1 or u.count(c_old) != 1:
        sys.exit('air-b2l: nir_lower_viewport_transform.c is not as expected')
    u = u.replace(a, b).replace(c_old, c_new)
    open(r, 'w').write(u)
print('air-b2l: nir_lower_viewport_transform takes store_output of the position')

# B1L-0067 (diagnostic): pandecode prints one blend descriptor on v6+ (the framebuffer's render target count is not
# known there); B2L_PANDECODE_RTS=N prints N of them
r = sys.argv[1] + '/../../panfrost/lib/genxml/decode_jm.c'
u = open(r).read()
a = """   struct pandecode_fbd fbd_info = {.rt_count = 1};
#endif

   if (PAN_ARCH >= 6 || (PAN_ARCH == 5 && job_type != MALI_JOB_TYPE_TILER)) {"""
b = """   struct pandecode_fbd fbd_info = {.rt_count = 1};
   if (getenv("B2L_PANDECODE_RTS"))
      fbd_info.rt_count = atoi(getenv("B2L_PANDECODE_RTS"));   /* B2L (B1L-0067) */
#endif

   if (PAN_ARCH >= 6 || (PAN_ARCH == 5 && job_type != MALI_JOB_TYPE_TILER)) {"""
if u.count('B1L-0067') == 0:
    if u.count(a) != 1:
        sys.exit('air-b2l: decode_jm.c is not as expected')
    u = u.replace(a, b)
    if '#include <stdlib.h>' not in u:
        u = '#include <stdlib.h>\n' + u
    open(r, 'w').write(u)
print('air-b2l: pandecode prints B2L_PANDECODE_RTS blend descriptors')
print('air-b2l: finalize_for_gallium gathers shader info')

# B1L-0070: a render pass that clears and draws nothing. Scion's contract clears only inside rwa_render_draw, so a pass
# with a load action of Clear and no draw (what CoreAnimation does to start a frame, and the M5 test of B1L-0068/69)
# left its attachments as they were. rwa_render_clear makes the pass's framebuffer, clears the aspects asked for and
# waits for the GPU, with the same rules as rwa_render_draw.
p = sys.argv[1] + '/air_metal.c'
s = open(p).read()
if 'rwa_render_clear' not in s:
    a = '''int
rwa_render_draw(struct rwa_render *render, const struct rwa_draw *draw,
                char *why, size_t why_size)
{'''
    b = '''/* B2L (B1L-0070): a pass with no draw in it, whose only work is its load actions. */
int
rwa_render_clear(struct rwa_device *device, const struct rwa_draw *draw, char *why, size_t why_size)
{
   struct pipe_context *pipe = device ? device->pipe : NULL;
   struct pipe_surface *surf[PIPE_MAX_COLOR_BUFS] = { 0 };
   struct pipe_surface *zsurf = NULL;
   struct pipe_resource *size_of = NULL;
   int rc = -1;

   if (!pipe || !draw || draw->color_count > PIPE_MAX_COLOR_BUFS) {
      say(why, why_size, "a clear with no device, no description or more than %u colour attachments",
          PIPE_MAX_COLOR_BUFS);
      return -1;
   }
   for (unsigned i = 0; i < draw->color_count; i++)
      if (draw->color[i] && !size_of)
         size_of = draw->color[i]->res;
   if (!size_of && draw->depth)
      size_of = draw->depth->res;
   if (!size_of)
      return 0;                 /* no attachment at all: nothing to clear */

   struct pipe_framebuffer_state fb = { .width = size_of->width0, .height = size_of->height0,
                                        .nr_cbufs = draw->color_count,
                                        .samples = size_of->nr_samples ? size_of->nr_samples : 1 };
   unsigned mask = 0;
   for (unsigned i = 0; i < draw->color_count; i++) {
      if (!draw->color[i])
         continue;
      surf[i] = pipe->create_surface(pipe, draw->color[i]->res,
         &(struct pipe_surface){ .format = draw->color[i]->res->format });
      if (!surf[i]) {
         say(why, why_size, "%s would not make a render surface of colour attachment %u",
             rwa_device_name(device), i);
         goto out;
      }
      fb.cbufs[i] = surf[i];
      if (draw->clear_color)
         mask |= PIPE_CLEAR_COLOR0 << i;
   }
   if (draw->depth) {
      zsurf = pipe->create_surface(pipe, draw->depth->res,
         &(struct pipe_surface){ .format = draw->depth->res->format });
      if (!zsurf) {
         say(why, why_size, "%s would not make a render surface of the depth attachment",
             rwa_device_name(device));
         goto out;
      }
      fb.zsbuf = zsurf;
      if (draw->clear_depth && format_has_depth(draw->depth->res->format))
         mask |= PIPE_CLEAR_DEPTH;
      if (draw->clear_stencil && format_has_stencil(draw->depth->res->format))
         mask |= PIPE_CLEAR_STENCIL;
   }
   if (!mask) {
      rc = 0;                   /* every load action is Load: the pass changes nothing */
      goto out;
   }
   b2l_fb_set(device, &fb);
   {
      union pipe_color_union clear;
      for (unsigned c = 0; c < 4; c++)
         clear.f[c] = draw->clear_color ? draw->clear_color[c] : 0.0f;
      pipe->clear(pipe, mask, NULL, &clear,
                  draw->clear_depth ? (double)*draw->clear_depth : 0.0,
                  draw->clear_stencil ? *draw->clear_stencil & 0xff : 0);
   }
   rc = b2l_sync_draws() ? finish_or_say_why(device, true, "clear", why, why_size) : 0;
out:
   for (unsigned i = 0; i < draw->color_count; i++)
      pipe_surface_reference(&surf[i], NULL);
   pipe_surface_reference(&zsurf, NULL);
   return rc;
}

int
rwa_render_draw(struct rwa_render *render, const struct rwa_draw *draw,
                char *why, size_t why_size)
{'''
    if s.count(a) != 1:
        sys.exit('air-b2l: rwa_render_draw is not as expected')
    s = s.replace(a, b)
    open(p, 'w').write(s)
h = sys.argv[1] + '/air_metal.h'
t = open(h).read()
if 'rwa_render_clear' not in t:
    a = '''int rwa_render_draw(struct rwa_render *render, const struct rwa_draw *draw,
		    char *why, size_t why_size);'''
    if t.count(a) != 1:
        sys.exit('air-b2l: air_metal.h is not as expected')
    t = t.replace(a, a + '''
/* B2L (B1L-0070): a pass without a draw: only `color`, `color_count`, `depth` and the three clears are read. */
int rwa_render_clear(struct rwa_device *device, const struct rwa_draw *draw, char *why, size_t why_size);''')
    open(h, 'w').write(t)
print('air-b2l: rwa_render_clear for passes without a draw')

# B1L-0074: one framebuffer per pass and no wait per draw.
#  - rwa_render_draw and rwa_render_clear made new surfaces at every call, set them and set an empty framebuffer
#    afterwards; panfrost keys its batch on the framebuffer, so every draw was a batch of its own (a tiler and a
#    fragment job over the whole target, with the previous contents loaded) -- and each waited for the GPU. The
#    framebuffer is now kept on the device while the attachments stay the same resources, so a pass's clear and
#    draws are ONE batch; the GPU is waited for once, by the command buffer (rwa_finish). B2L_METAL_SYNC_DRAWS=1
#    restores a wait after every draw.
#  - rwa_flush: submit what is recorded without waiting, called by the driver at -commit.
p = sys.argv[1] + '/air_metal.c'
s = open(p).read()
if 'b2l_fb_set' in s and 'b2l_fb_same' not in s:
    a = '#include "util/u_inlines.h"\n'
    if s.count(a) != 1:
        sys.exit('air-b2l: includes are not as expected')
    s = s.replace(a, a + '#include "util/u_framebuffer.h"\n')
    a = """   struct rwa_buffer *encoded;
};"""
    if s.count(a) != 1:
        sys.exit('air-b2l: struct rwa_device is not as expected')
    s = s.replace(a, """   struct rwa_buffer *encoded;

   /* B2L (B1L-0074): the framebuffer the context holds between draws, with references of its own */
   struct pipe_framebuffer_state b2l_fb;
};""")
    a = """void
rwa_texture_destroy(struct rwa_texture *texture)
{
   if (!texture)
      return;
"""
    if s.count(a) != 1:
        sys.exit('air-b2l: rwa_texture_destroy is not as expected')
    s = s.replace(a, """/* B2L (B1L-0074) */
static bool
b2l_sync_draws(void)
{
   static int sync = -1;
   if (sync < 0)
      sync = getenv("B2L_METAL_SYNC_DRAWS") != NULL;
   return sync;
}

static bool
b2l_surf_same(const struct pipe_surface *a, const struct pipe_surface *b)
{
   if (!a || !b)
      return a == b;
   return a->texture == b->texture && a->format == b->format && a->u.tex.level == b->u.tex.level &&
          a->u.tex.first_layer == b->u.tex.first_layer && a->u.tex.last_layer == b->u.tex.last_layer;
}

static bool
b2l_fb_same(const struct pipe_framebuffer_state *a, const struct pipe_framebuffer_state *b)
{
   if (a->width != b->width || a->height != b->height || a->nr_cbufs != b->nr_cbufs ||
       a->samples != b->samples || !b2l_surf_same(a->zsbuf, b->zsbuf))
      return false;
   for (unsigned i = 0; i < a->nr_cbufs; i++)
      if (!b2l_surf_same(a->cbufs[i], b->cbufs[i]))
         return false;
   return true;
}

/* Set `fb` unless the context already holds the same attachments, in which case the pass goes on in the batch it
 * is in. The device keeps its own references, so the caller releases its surfaces with pipe_surface_reference. */
static void
b2l_fb_set(struct rwa_device *device, const struct pipe_framebuffer_state *fb)
{
   if (!b2l_sync_draws() && device->b2l_fb.width && b2l_fb_same(&device->b2l_fb, fb))
      return;
   device->pipe->set_framebuffer_state(device->pipe, fb);
   util_copy_framebuffer_state(&device->b2l_fb, fb);
}

/* Forget the kept framebuffer when one of its resources is going away (or always, with res NULL). */
static void
b2l_fb_forget(struct rwa_device *device, const struct pipe_resource *res)
{
   bool uses = !res;
   if (!device || !device->b2l_fb.width)
      return;
   for (unsigned i = 0; i < device->b2l_fb.nr_cbufs; i++)
      if (device->b2l_fb.cbufs[i] && device->b2l_fb.cbufs[i]->texture == res)
         uses = true;
   if (device->b2l_fb.zsbuf && device->b2l_fb.zsbuf->texture == res)
      uses = true;
   if (!uses)
      return;
   device->pipe->set_framebuffer_state(device->pipe, &(struct pipe_framebuffer_state){ 0 });
   util_unreference_framebuffer_state(&device->b2l_fb);
   memset(&device->b2l_fb, 0, sizeof(device->b2l_fb));
}

int
rwa_flush(struct rwa_device *device)
{
   if (!device || !device->pipe)
      return -1;
   device->pipe->flush(device->pipe, NULL, 0);
   return 0;
}

void
rwa_texture_destroy(struct rwa_texture *texture)
{
   if (!texture)
      return;
   b2l_fb_forget(texture->device, texture->res);   /* B2L (B1L-0074) */
""")
    a = """   pipe->set_framebuffer_state(pipe, &fb);

   /* The viewport, and now its z as well."""
    if s.count(a) != 1:
        sys.exit('air-b2l: the draw framebuffer is not as expected')
    s = s.replace(a, """   b2l_fb_set(device, &fb);   /* B2L (B1L-0074) */

   /* The viewport, and now its z as well.""")
    a = """   rc = finish_or_say_why(device, true, "draw", why, why_size);"""
    if s.count(a) != 1:
        sys.exit('air-b2l: the draw finish is not as expected')
    s = s.replace(a, """   rc = b2l_sync_draws() ? finish_or_say_why(device, true, "draw", why, why_size) : 0;   /* B2L (B1L-0074) */""")
    a = """   pipe->set_framebuffer_state(pipe, &(struct pipe_framebuffer_state){ 0 });
   for (unsigned i = 0; i < draw->color_count; i++)
      if (surf[i])
         pipe->surface_destroy(pipe, surf[i]);
   if (zsurf)
      pipe->surface_destroy(pipe, zsurf);
   return rc;
}"""
    if s.count(a) != 1:
        sys.exit('air-b2l: the draw tail is not as expected')
    s = s.replace(a, """   /* B2L (B1L-0074): the context and the device keep the framebuffer for the next draw of the pass */
   if (b2l_sync_draws())
      b2l_fb_forget(device, NULL);
   for (unsigned i = 0; i < draw->color_count; i++)
      pipe_surface_reference(&surf[i], NULL);
   pipe_surface_reference(&zsurf, NULL);
   return rc;
}""")
    open(p, 'w').write(s)
h = sys.argv[1] + '/air_metal.h'
t = open(h).read()
if 'rwa_flush' not in t:
    a = 'int rwa_render_clear(struct rwa_device *device, const struct rwa_draw *draw, char *why, size_t why_size);'
    if t.count(a) != 1:
        sys.exit('air-b2l: air_metal.h has no rwa_render_clear')
    t = t.replace(a, a + '\n/* B2L (B1L-0074): submit what is recorded, without waiting for it */\nint rwa_flush(struct rwa_device *device);')
    open(h, 'w').write(t)
print('air-b2l: one framebuffer per pass, no wait per draw, rwa_flush')

# B1L-0082 (M6b): an inline sampler (constexpr sampler) whose state is not a single integer. QuartzCore's shaders
# (iOS 26) write the two-word form -- an aggregate whose first word is the state and whose second carries the LOD
# bias -- and the translator refused every pipeline that held one ("an inline sampler whose state is not a constant"),
# so backboardd aborted (B1L-0081). The first word is the state, in the one-word layout; the LOD bias is not applied.
# A form still not understood is refused with its kind in the sentence.
t = sys.argv[1] + '/air_to_nir.c'
u = open(t).read()
if 'b2l_sampler_word' not in u:
    a = """struct air_sampler_state
air_decode_sampler_state(uint64_t w)
{"""
    b = """/* B2L (B1L-0082): the state word of an inline sampler, from a plain integer or from the two-word form */
static bool
b2l_sampler_word(const struct air_value *v, uint64_t *w, unsigned depth)
{
   if (air_const_uint(v, w))
      return true;
   const struct air_constant *k = air_value_as_constant(v);
   if (!k || depth > 3)
      return false;
   if (k->ckind == AIR_CONST_NULL) {
      *w = 0;
      return true;
   }
   if (k->ckind == AIR_CONST_AGGREGATE && k->num_elems && k->elems)
      return b2l_sampler_word(k->elems[0], w, depth + 1);
   if (k->ckind == AIR_CONST_DATA && k->data) {
      *w = k->data[0];
      return true;
   }
   return false;
}

struct air_sampler_state
air_decode_sampler_state(uint64_t w)
{"""
    if u.count(a) != 1:
        sys.exit('air-b2l: air_decode_sampler_state is not as expected')
    u = u.replace(a, b)
    a = """         uint64_t word = 0;
         if (g->init && !air_const_uint(g->init, &word)) {
            air_error(c, "interface: an inline sampler whose state is not a constant");
            return;
         }"""
    b = """         uint64_t word = 0;
         if (g->init && !b2l_sampler_word(g->init, &word, 0)) {
            const struct air_constant *k = air_value_as_constant(g->init);
            air_error(c, "interface: an inline sampler whose state is not a constant (B2L: constant kind %d, "
                         "type kind %d)", k ? (int)k->ckind : -1, g->init->type ? (int)g->init->type->kind : -1);
            return;
         }"""
    if u.count(a) != 1:
        sys.exit('air-b2l: the inline sampler refusal is not as expected')
    u = u.replace(a, b)
    open(t, 'w').write(u)
print('air-b2l: inline samplers in the two-word form')

# B1L-0083 (M6b): the next two refusals of QuartzCore's pipelines in backboardd (B1L-0082).
# 1. air.get_read_sampler: the sampler a texture.read() goes through -- the texel at an integer coordinate, no
#    filtering: an inline sampler of the shader's own, appended once (clamp to edge, nearest, coord::pixel, no mips).
# 2. A call passing a pointer at a computed offset (a member of a struct, an element of an array): the offset is a
#    def of the CALLER and could not be used in the callee, so it was refused. It now goes to the callee as one more
#    32-bit parameter, the pointer's class and binding staying the caller's resolution (as for a texture argument);
#    nir_inline_functions puts the caller's def back in place of the parameter.
i = sys.argv[1] + '/air_intrinsics.c'
u = open(i).read()
if 'b2l_read_sampler' not in u:
    a = """   const char *op = n.part[0];
   if (strstr(op, "texture") || strstr(op, "_depth_") || !strncmp(op, "sample", 6) ||"""
    b = """   /* B2L (B1L-0083): the sampler of texture.read(), an inline sampler of the shader's own */
   if (!strcmp(name, "air.get_read_sampler")) {
      struct air_shader_info *info = c->info;
      unsigned index = ~0u, next = 0;
      for (unsigned k = 0; k < info->num_resources; k++) {
         if (info->resources[k].kind != AIR_RESOURCE_SAMPLER)
            continue;
         next = MAX2(next, info->resources[k].index + 1);
         if (info->resources[k].name && !strcmp(info->resources[k].name, "b2l_read_sampler"))
            index = info->resources[k].index;
      }
      if (index == ~0u) {
         /* clamp to edge on s, t, r; nearest; no mips; coord::pixel; LOD range [0, 65504] */
         uint64_t word = 1ull | (1ull << 3) | (1ull << 6) | (1ull << 15) | (0x7bffull << 40);
         index = next;
         info->resources = reralloc(c->mem_ctx, info->resources, struct air_resource,
                                    info->num_resources + 1);
         info->resources[info->num_resources++] = (struct air_resource) {
            .kind = AIR_RESOURCE_SAMPLER, .index = index, .read = true,
            .param = ~0u, .name = "b2l_read_sampler",
            .inline_sampler = true, .sampler_state = word,
            .sampler = air_decode_sampler_state(word),
            .raster_order_group = -1,
            .argbuf_set = ~0u, .argbuf_id = ~0u,
         };
      }
      struct air_ptr p = { .class = AIR_PTR_SAMPLER, .binding = index };
      air_set(c, &in->base, air_val_ptr(c, p));
      return true;
   }

   const char *op = n.part[0];
   if (strstr(op, "texture") || strstr(op, "_depth_") || !strncmp(op, "sample", 6) ||"""
    if u.count(a) != 1:
        sys.exit('air-b2l: air_translate_call is not as expected')
    u = u.replace(a, b)
    open(i, 'w').write(u)
t = sys.argv[1] + '/air_to_nir.c'
u = open(t).read()
if 'B1L-0083' not in u:
    reps = [
    ("""      bool ptr = argv && argv[i] && (argv[i]->kind == AIR_VAL_PTR ||
                                     val_holds_ptr(argv[i]));
      if (ptr && argv[i]->kind == AIR_VAL_PTR && argv[i]->ptr.offset) {
         air_error(c, "intrinsic: a call passing a pointer at a computed offset as "
                      "argument %u, which would use the caller's own value here", i);
         return NULL;
      }""",
     """      bool ptr = argv && argv[i] && (argv[i]->kind == AIR_VAL_PTR ||
                                     val_holds_ptr(argv[i]));
      /* B2L (B1L-0083): a pointer at a computed offset passes that offset as a parameter */
      if (ptr && argv[i]->kind == AIR_VAL_PTR && argv[i]->ptr.offset) {
         is_param[i] = true;
         specialised = true;
         continue;
      }"""),
    ("""   for (unsigned i = 0; i < f->num_args; i++) {
      if (!is_param[i])
         continue;
      const struct air_type *at = f->args[i].type;
      fn->params[idx++] = (nir_parameter){
         .num_components = (uint8_t)air_nir_components(at),
         .bit_size = (uint8_t)air_nir_bit_size(c, at),
      };""",
     """   for (unsigned i = 0; i < f->num_args; i++) {
      if (!is_param[i])
         continue;
      const struct air_type *at = f->args[i].type;
      if (argv && argv[i] && argv[i]->kind == AIR_VAL_PTR && argv[i]->ptr.offset) {
         fn->params[idx++] = (nir_parameter){ .num_components = 1, .bit_size = 32 };   /* B2L (B1L-0083) */
         continue;
      }
      fn->params[idx++] = (nir_parameter){
         .num_components = (uint8_t)air_nir_components(at),
         .bit_size = (uint8_t)air_nir_bit_size(c, at),
      };"""),
    ("""      if (!is_param[i]) {
         air_set(c, &f->args[i], argv[i]);   /* the caller's own resolution */
         continue;
      }
      air_set(c, &f->args[i], air_val_ssa(c, nir_load_param(&c->b, pidx++)));""",
     """      if (!is_param[i]) {
         air_set(c, &f->args[i], argv[i]);   /* the caller's own resolution */
         continue;
      }
      if (argv && argv[i] && argv[i]->kind == AIR_VAL_PTR && argv[i]->ptr.offset) {
         struct air_ptr p = argv[i]->ptr;   /* B2L (B1L-0083): the caller's resolution, its offset a parameter */
         p.offset = nir_load_param(&c->b, pidx++);
         air_set(c, &f->args[i], air_val_ptr(c, p));
         continue;
      }
      air_set(c, &f->args[i], air_val_ssa(c, nir_load_param(&c->b, pidx++)));"""),
    ("""   for (unsigned i = 0; i < f->num_args; i++) {
      if (!is_param[i])
         continue;
      nir_def *a = air_get_ssa(c, in->operands[i + 1]);""",
     """   for (unsigned i = 0; i < f->num_args; i++) {
      if (!is_param[i])
         continue;
      if (argv[i] && argv[i]->kind == AIR_VAL_PTR && argv[i]->ptr.offset) {
         call->params[idx++] = nir_src_for_ssa(argv[i]->ptr.offset);   /* B2L (B1L-0083) */
         continue;
      }
      nir_def *a = air_get_ssa(c, in->operands[i + 1]);"""),
    ]
    for a, b in reps:
        if u.count(a) != 1:
            sys.exit('air-b2l: B1L-0083 call patch: text not as expected: ' + a[:60])
        u = u.replace(a, b)
    open(t, 'w').write(u)
print('air-b2l: air.get_read_sampler; pointers at a computed offset passed to callees')

# B1L-0084 (M6b): insertelement at an index known only at run time (QuartzCore, B1L-0083): NIR's nir_vector_insert
# selects component by component, which is what the translator only refused before.
t = sys.argv[1] + '/air_to_nir.c'
u = open(t).read()
if 'B1L-0084' not in u:
    a = """      if (!nir_src_is_const(nir_src_for_ssa(i)))
         return air_error(c, "instruction: insertelement at a run-time index");"""
    b = """      if (!nir_src_is_const(nir_src_for_ssa(i))) {   /* B2L (B1L-0084) */
         air_set(c, &in->base, air_val_ssa(c, nir_vector_insert(&c->b, v, x, nir_u2u32(&c->b, i))));
         return true;
      }"""
    if u.count(a) != 1:
        sys.exit('air-b2l: insertelement is not as expected')
    u = u.replace(a, b)
    open(t, 'w').write(u)
print('air-b2l: insertelement at a run-time index')

# B1L-0084 (M6b): AIR 2.8's texture reads (iOS 26). air.read_texture_2d is now
#   { <4 x half>, i8 } (ptr as(1) texture, ptr as(2) sampler, <2 x i32> coord, <2 x i32> offset, i32 lod, i32)
# -- the read sampler (air.get_read_sampler) right after the texture and a texel offset after the coordinate. The
# translator read the sampler as the coordinate ("a pointer where a number belongs": 16 of QuartzCore's 189 functions,
# found with air2nir on the Mac). Both are recognised by their kind, so the older form still reads as before.
i = sys.argv[1] + '/air_intrinsics.c'
u = open(i).read()
if 'read_with_sampler' not in u:
    reps = [
    ("""   if (have_sampler) {
      if (!tex_handle(c, in, arg, AIR_PTR_SAMPLER, &samp))
         return false;
      arg++;
   }
""", """   if (have_sampler) {
      if (!tex_handle(c, in, arg, AIR_PTR_SAMPLER, &samp))
         return false;
      arg++;
   }
   /* B2L (B1L-0084): AIR 2.8's read takes the read sampler right after the texture */
   bool read_with_sampler = false;
   if (!have_sampler && !strcmp(t.op, "read") && in->num_operands > arg) {
      struct air_val *sv = air_get(c, in->operands[arg]);
      if (sv && sv->kind == AIR_VAL_PTR && sv->ptr.class == AIR_PTR_SAMPLER) {
         read_with_sampler = true;
         arg++;
      }
   }
"""),
    ("""   if (!strcmp(t.op, "read") && tex.is_image) {
      uint64_t extra;
      if (!tex_flag(c, in, arg, &extra))""", """   if (!strcmp(t.op, "read") && tex.is_image) {
      uint64_t extra;
      /* B2L (B1L-0084): and a texel offset after the coordinate, which an image load cannot take */
      if (read_with_sampler && in->num_operands > arg && in->operands[arg]->type &&
          in->operands[arg]->type->kind == AIR_TYPE_VECTOR) {
         nir_def *off = air_get_ssa(c, in->operands[arg++]);
         if (!off)
            return false;
         if (!all_zero(off))
            return air_error(c, "intrinsic: an image read with a texel offset");
      }
      if (!tex_flag(c, in, arg, &extra))"""),
    ("""      nir_def *which = air_get_ssa(c, in->operands[arg++]);
      if (!which)
         return false;
      which = nir_u2u32(&c->b, which);
      op = t.is_ms ? nir_texop_txf_ms : nir_texop_txf;""", """      /* B2L (B1L-0084): AIR 2.8's texel offset, between the coordinate and the level */
      if (read_with_sampler && in->num_operands > arg && in->operands[arg]->type &&
          in->operands[arg]->type->kind == AIR_TYPE_VECTOR) {
         nir_def *off = air_get_ssa(c, in->operands[arg++]);
         if (!off)
            return false;
         if (!all_zero(off))
            srcs[nsrc++] = (nir_tex_src){ .src_type = nir_tex_src_offset,
                                          .src = nir_src_for_ssa(nir_u2u32(&c->b, off)) };
      }
      nir_def *which = air_get_ssa(c, in->operands[arg++]);
      if (!which)
         return false;
      which = nir_u2u32(&c->b, which);
      op = t.is_ms ? nir_texop_txf_ms : nir_texop_txf;"""),
    ]
    for a, b in reps:
        if u.count(a) != 1:
            sys.exit('air-b2l: B1L-0084 read patch: text not as expected: ' + a[:60])
        u = u.replace(a, b)
    open(i, 'w').write(u)
print('air-b2l: AIR 2.8 texture reads (read sampler, texel offset)')

# B1L-0084 (M6b): air.get_null_texture_* -- QuartzCore's main fragment shader (fixed_frag_lph_cpf/cph) picks a null
# texture where a function constant says an attachment or an image is absent, and never samples it then. A texture
# at slot 127, which nothing binds and which the specialised shader does not reach, stands for it.
i = sys.argv[1] + '/air_intrinsics.c'
u = open(i).read()
if 'get_null_texture' not in u:
    a = """   /* B2L (B1L-0083): the sampler of texture.read(), an inline sampler of the shader's own */"""
    b = """   /* B2L (B1L-0084): a null texture, chosen where a function constant leaves a texture out */
   if (!strncmp(name, "air.get_null_texture", 20)) {
      struct air_ptr p = { .class = AIR_PTR_TEXTURE, .binding = 127 };
      air_set(c, &in->base, air_val_ptr(c, p));
      return true;
   }
""" + a
    if u.count(a) != 1:
        sys.exit('air-b2l: the get_read_sampler branch is not where B1L-0084 expects it')
    u = u.replace(a, b)
    open(i, 'w').write(u)
print('air-b2l: air.get_null_texture_*')

# B1L-0084 (M6b): a phi between a texture and the null texture (QuartzCore's main fragment shader: the real attachment
# when a function constant says it is there, the null one otherwise). The null side is never sampled, so the phi is
# the real texture; it is the null one only when nothing else comes in.
t = sys.argv[1] + '/air_to_nir.c'
u = open(t).read()
if 'null_tex' not in u:
    a = """      if (!first)
         first = v;
      else if (!same_ptr(&first->ptr, &v->ptr))
         return air_error(c, "instruction: a phi between pointers that name different "
                             "places, which would have to be chosen at run time");
   }
   if (!first)
      return air_error(c, "instruction: a phi of pointers with no reachable operand");"""
    b = """      if (v->ptr.class == AIR_PTR_TEXTURE && v->ptr.binding == 127) {   /* B2L (B1L-0084): the null texture */
         null_tex = v;
         continue;
      }
      if (!first)
         first = v;
      else if (!same_ptr(&first->ptr, &v->ptr))
         return air_error(c, "instruction: a phi between pointers that name different "
                             "places, which would have to be chosen at run time");
   }
   if (!first)
      first = null_tex;
   if (!first)
      return air_error(c, "instruction: a phi of pointers with no reachable operand");"""
    if u.count(a) != 1:
        sys.exit('air-b2l: resolve_pointer_phi is not as expected')
    u = u.replace(a, b)
    a = """resolve_pointer_phi(struct air_ctx *c, const struct air_inst *in, const bool *reachable)
{
   struct air_val *first = NULL;"""
    if u.count(a) != 1:
        sys.exit('air-b2l: resolve_pointer_phi head is not as expected')
    u = u.replace(a, a + "\n   struct air_val *null_tex = NULL;   /* B2L (B1L-0084) */")
    open(t, 'w').write(u)
print('air-b2l: a phi between a texture and the null texture')

# B1L-0084 (M6b): insertvalue into a member that does not exist yet in the value -- QuartzCore writes the padding of
# its vertex output (StrokeCurveVertexOutput's [8 x i8], member 5) byte by byte into a struct built from poison, and its
# main fragment shader does the same kind of thing; agg_slot refused ("an aggregate index out of its value"). An
# empty level on the path is made, from the aggregate's type, before the slot is looked up; nothing that has a value
# is replaced.
t = sys.argv[1] + '/air_to_nir.c'
u = open(t).read()
if 'b2l_agg_make_path' not in u:
    a = """/* an aggregate value's field, by the index path of extract/insertvalue */"""
    b = """/* B2L (B1L-0084): the empty levels of an insertvalue's path, made from the aggregate's type */
static void
b2l_agg_make_path(struct air_ctx *c, struct air_val **root, const struct air_type *t, const uint32_t *idx,
                  unsigned n)
{
   struct air_val **slot = root;
   for (unsigned i = 0; i < n && t; i++) {
      unsigned count = t->kind == AIR_TYPE_STRUCT ? t->num_members :
                       t->kind == AIR_TYPE_ARRAY ? (unsigned)t->count : 0;
      if (!count)
         return;
      if (!*slot || (*slot)->kind == AIR_VAL_NONE)
         *slot = air_val_agg(c, count);
      if ((*slot)->kind != AIR_VAL_AGG || idx[i] >= (*slot)->num_elems)
         return;
      const struct air_type *next = t->kind == AIR_TYPE_STRUCT ? t->members[idx[i]] : t->elem;
      slot = &(*slot)->elems[idx[i]];
      t = next;
   }
}

""" + a
    if u.count(a) != 1:
        sys.exit('air-b2l: agg_slot is not where B1L-0084 expects it')
    u = u.replace(a, b)
    a = """      struct air_val *copy = clone_val(c, agg);
      struct air_val **slot = agg_slot(c, copy, in->indices, in->num_indices);"""
    if u.count(a) != 1:
        sys.exit('air-b2l: insertvalue is not as expected')
    u = u.replace(a, """      struct air_val *copy = clone_val(c, agg);
      b2l_agg_make_path(c, &copy, in->operands[0]->type, in->indices, in->num_indices);   /* B2L (B1L-0084) */
      struct air_val **slot = agg_slot(c, copy, in->indices, in->num_indices);""")
    open(t, 'w').write(u)
print('air-b2l: insertvalue into a member not made yet')

# B1L-0084 (M6b): a call passing "this buffer, or null" (QuartzCore's main fragment shader: `select i1 %fc, ptr null,
# ptr %18`, by a function constant). The callee uses the pointer only where it is not null, so it gets the pointer.
t = sys.argv[1] + '/air_to_nir.c'
u = open(t).read()
if 'a choice between a pointer and null' not in u:
    a = """      bool ptr = argv && argv[i] && (argv[i]->kind == AIR_VAL_PTR ||
                                     val_holds_ptr(argv[i]));"""
    b = """      /* B2L (B1L-0084): a choice between a pointer and null passes the pointer */
      if (argv && argv[i] && argv[i]->kind == AIR_VAL_PTR_SELECT && argv[i]->num_elems == 2) {
         struct air_val *x = argv[i]->elems[0], *y = argv[i]->elems[1];
         if (x && x->kind == AIR_VAL_PTR && x->ptr.class == AIR_PTR_NONE && y && y->kind == AIR_VAL_PTR)
            argv[i] = y;
         else if (y && y->kind == AIR_VAL_PTR && y->ptr.class == AIR_PTR_NONE && x && x->kind == AIR_VAL_PTR)
            argv[i] = x;
      }
""" + a
    if u.count(a) != 1:
        sys.exit('air-b2l: translate_callee arguments are not as expected')
    u = u.replace(a, b)
    open(t, 'w').write(u)
print('air-b2l: a call passing a pointer or null')

# B1L-0084 (M6b): a select between two POINTERS whose condition is known while translating. QuartzCore's main fragment
# shader picks its sampler with `select i1 %fc_cond, ptr @__air_sampler_state, ptr %39` and passes the choice to a
# callee, which was refused as "a pointer chosen at run time". Function constants are immediates here (their values
# come with the pipeline), so the condition is an expression of constants: it is evaluated (load_const and ALU
# operations over evaluable sources, as nir_opt_constant_folding would) and the pointer it picks is kept.
t = sys.argv[1] + '/air_to_nir.c'
u = open(t).read()
if 'b2l_eval_const' not in u:
    a = '#include "util/hash_table.h"\n'
    if u.count(a) != 1:
        sys.exit('air-b2l: air_to_nir.c includes are not as expected')
    u = u.replace(a, a + '#include "nir_constant_expressions.h"\n')
    a = """/* B2L (B1L-0084): the empty levels of an insertvalue's path, made from the aggregate's type */"""
    b = """/* B2L (B1L-0084): the value of a def that is an expression of constants, component by component */
static bool
b2l_eval_const(nir_def *def, nir_const_value *out, unsigned depth)
{
   if (depth > 24 || def->num_components > NIR_MAX_VEC_COMPONENTS)
      return false;
   nir_instr *ins = def->parent_instr;
   if (ins->type == nir_instr_type_load_const) {
      nir_load_const_instr *lc = nir_instr_as_load_const(ins);
      for (unsigned k = 0; k < def->num_components; k++)
         out[k] = lc->value[k];
      return true;
   }
   /* a function constant reaches a condition through the program-scope variable the static initializer copied it
    * into: a load_scratch at a fixed offset is the value of the one store at that offset in the function, when there
    * is exactly one, no store at an unknown or overlapping offset, and the stored value is itself known */
   if (ins->type == nir_instr_type_intrinsic) {
      nir_intrinsic_instr *ld = nir_instr_as_intrinsic(ins);
      if (ld->intrinsic != nir_intrinsic_load_scratch || !nir_src_is_const(ld->src[0]))
         return false;
      uint64_t off = nir_src_as_uint(ld->src[0]);
      uint64_t size = def->num_components * MAX2(def->bit_size / 8, 1);
      nir_function_impl *impl = nir_cf_node_get_function(&ins->block->cf_node);
      nir_intrinsic_instr *only = NULL;
      unsigned n = 0;
      nir_foreach_block_unstructured(blk, impl) {
         nir_foreach_instr(i2, blk) {
            if (i2->type != nir_instr_type_intrinsic)
               continue;
            nir_intrinsic_instr *st = nir_instr_as_intrinsic(i2);
            if (st->intrinsic != nir_intrinsic_store_scratch)
               continue;
            if (!nir_src_is_const(st->src[1]))
               return false;
            uint64_t so = nir_src_as_uint(st->src[1]);
            uint64_t ss = st->src[0].ssa->num_components * MAX2(st->src[0].ssa->bit_size / 8, 1);
            if (so == off && ss == size) {
               only = st;
               n++;
            } else if (so < off + size && off < so + ss) {
               return false;
            }
         }
      }
      if (n != 1 || only->src[0].ssa->bit_size != def->bit_size ||
          only->src[0].ssa->num_components != def->num_components)
         return false;
      return b2l_eval_const(only->src[0].ssa, out, depth + 1);
   }
   if (ins->type != nir_instr_type_alu)
      return false;
   nir_alu_instr *alu = nir_instr_as_alu(ins);
   const nir_op_info *info = &nir_op_infos[alu->op];
   nir_const_value srcv[NIR_MAX_VEC_COMPONENTS][NIR_MAX_VEC_COMPONENTS];
   nir_const_value *srcs[NIR_MAX_VEC_COMPONENTS];
   unsigned bit_size = 0;
   if (!nir_alu_type_get_type_size(info->output_type))
      bit_size = alu->def.bit_size;
   for (unsigned i = 0; i < info->num_inputs; i++) {
      nir_const_value whole[NIR_MAX_VEC_COMPONENTS];
      if (!b2l_eval_const(alu->src[i].src.ssa, whole, depth + 1))
         return false;
      if (bit_size == 0 && !nir_alu_type_get_type_size(info->input_types[i]))
         bit_size = alu->src[i].src.ssa->bit_size;
      unsigned n = nir_ssa_alu_instr_src_components(alu, i);
      for (unsigned j = 0; j < n; j++)
         srcv[i][j] = whole[alu->src[i].swizzle[j]];
      srcs[i] = srcv[i];
   }
   if (bit_size == 0)
      bit_size = 32;
   nir_eval_const_opcode(alu->op, out, alu->def.num_components, bit_size, srcs, 0);
   return true;
}

""" + a
    if u.count(a) != 1:
        sys.exit('air-b2l: B1L-0084 helpers are not where the evaluator goes')
    u = u.replace(a, b)
    a = """         nir_def *pc = air_get_ssa(c, in->operands[0]);
         if (!pc)
            return false;
         if (pc->num_components != 1)
            return air_error(c, "instruction: a vector select between pointers");
         air_set(c, &in->base, air_val_ptr_select(c, pc, pa, pb));"""
    b = """         nir_def *pc = air_get_ssa(c, in->operands[0]);
         if (!pc)
            return false;
         if (pc->num_components != 1)
            return air_error(c, "instruction: a vector select between pointers");
         nir_const_value cv[NIR_MAX_VEC_COMPONENTS];   /* B2L (B1L-0084): a condition known now picks now */
         if (b2l_eval_const(pc, cv, 0)) {
            air_set(c, &in->base, nir_const_value_as_uint(cv[0], pc->bit_size) ? pa : pb);
            return true;
         }
         air_set(c, &in->base, air_val_ptr_select(c, pc, pa, pb));"""
    if u.count(a) != 1:
        sys.exit('air-b2l: the pointer select is not as expected')
    u = u.replace(a, b)
    open(t, 'w').write(u)
print('air-b2l: pointer selects whose condition is known while translating')

# B1L-0084 (M6b): a pointer chosen at run time, passed to a callee and sampled with. QuartzCore's main fragment
# shader picks its sampler -- its own inline one, or the pipeline's -- by a function constant that reaches the
# condition through the static initializer (a separate function until nir_inline_functions), so the choice cannot be
# made while translating. It is passed as it is: the condition as a 32-bit parameter, the two pointers as the caller
# resolved them. A texture operation whose sampler is such a choice is translated once with each sampler and the
# results selected; once the pipeline's constants are folded, NIR removes the side not taken.
t = sys.argv[1] + '/air_to_nir.c'
u = open(t).read()
if 'B2L_SELECT_ARG' not in u:
    def is_sel(v):
        return v
    cond = """argv[i]->kind == AIR_VAL_PTR_SELECT && argv[i]->num_elems == 2 &&
          argv[i]->elems[0] && argv[i]->elems[0]->kind == AIR_VAL_PTR &&
          argv[i]->elems[1] && argv[i]->elems[1]->kind == AIR_VAL_PTR"""
    reps = [
    ("""      bool ptr = argv && argv[i] && (argv[i]->kind == AIR_VAL_PTR ||
                                     val_holds_ptr(argv[i]));""",
     """      if (argv && argv[i] && """ + cond + """) {   /* B2L_SELECT_ARG (B1L-0084) */
         is_param[i] = true;
         specialised = true;
         continue;
      }
      bool ptr = argv && argv[i] && (argv[i]->kind == AIR_VAL_PTR ||
                                     val_holds_ptr(argv[i]));"""),
    ("""      if (argv && argv[i] && argv[i]->kind == AIR_VAL_PTR && argv[i]->ptr.offset) {
         fn->params[idx++] = (nir_parameter){ .num_components = 1, .bit_size = 32 };   /* B2L (B1L-0083) */
         continue;
      }""",
     """      if (argv && argv[i] && argv[i]->kind == AIR_VAL_PTR_SELECT) {   /* B2L (B1L-0084): condition, two offsets */
         for (unsigned q = 0; q < 3; q++)
            fn->params[idx++] = (nir_parameter){ .num_components = 1, .bit_size = 32 };
         continue;
      }
      if (argv && argv[i] && argv[i]->kind == AIR_VAL_PTR && argv[i]->ptr.offset) {
         fn->params[idx++] = (nir_parameter){ .num_components = 1, .bit_size = 32 };   /* B2L (B1L-0083) */
         continue;
      }"""),
    ("""      if (argv && argv[i] && argv[i]->kind == AIR_VAL_PTR && argv[i]->ptr.offset) {
         struct air_ptr p = argv[i]->ptr;   /* B2L (B1L-0083): the caller's resolution, its offset a parameter */""",
     """      if (argv && argv[i] && argv[i]->kind == AIR_VAL_PTR_SELECT) {   /* B2L (B1L-0084): the condition is a parameter */
         nir_def *cnd = nir_ine_imm(&c->b, nir_load_param(&c->b, pidx++), 0);
         struct air_val *side[2];
         for (unsigned q = 0; q < 2; q++) {
            struct air_ptr p = argv[i]->elems[q]->ptr;
            nir_def *off = nir_load_param(&c->b, pidx++);
            if (p.offset)
               p.offset = off;
            side[q] = air_val_ptr(c, p);
         }
         air_set(c, &f->args[i], air_val_ptr_select(c, cnd, side[0], side[1]));
         continue;
      }
      if (argv && argv[i] && argv[i]->kind == AIR_VAL_PTR && argv[i]->ptr.offset) {
         struct air_ptr p = argv[i]->ptr;   /* B2L (B1L-0083): the caller's resolution, its offset a parameter */"""),
    ("""      if (argv[i] && argv[i]->kind == AIR_VAL_PTR && argv[i]->ptr.offset) {
         call->params[idx++] = nir_src_for_ssa(argv[i]->ptr.offset);   /* B2L (B1L-0083) */
         continue;
      }""",
     """      if (argv[i] && argv[i]->kind == AIR_VAL_PTR_SELECT) {   /* B2L (B1L-0084): condition, two offsets */
         call->params[idx++] = nir_src_for_ssa(nir_b2i32(&c->b, argv[i]->def));
         for (unsigned q = 0; q < 2; q++) {
            nir_def *off = argv[i]->elems[q]->ptr.offset;
            call->params[idx++] = nir_src_for_ssa(off ? nir_u2u32(&c->b, off) : nir_imm_int(&c->b, 0));
         }
         continue;
      }
      if (argv[i] && argv[i]->kind == AIR_VAL_PTR && argv[i]->ptr.offset) {
         call->params[idx++] = nir_src_for_ssa(argv[i]->ptr.offset);   /* B2L (B1L-0083) */
         continue;
      }"""),
    ]
    reps.append(("""   unsigned nparams = f->num_args + nret;""",
                 """   unsigned nparams = 3 * f->num_args + nret;   /* B2L (B1L-0084): a chosen pointer takes three */"""))
    for a, b in reps:
        if u.count(a) != 1:
            sys.exit('air-b2l: B1L-0084 select-argument patch: text not as expected: ' + a[:70])
        u = u.replace(a, b)
    a = """static nir_function *
translate_callee(struct air_ctx *c, const struct air_function *f,"""
    if u.count(a) != 1:
        sys.exit('air-b2l: translate_callee is not where B1L-0084 expects it')
    u = u.replace(a, """/* B2L (B1L-0084): a resolved pointer whose offset is none or a constant, so the callee can have its own copy */
static bool
b2l_ptr_fixed(const struct air_val *v)
{
   return v && v->kind == AIR_VAL_PTR && (!v->ptr.offset || nir_src_is_const(nir_src_for_ssa(v->ptr.offset)));
}

static struct air_val *
b2l_ptr_here(struct air_ctx *c, struct air_val *v)
{
   if (!v->ptr.offset)
      return v;
   struct air_ptr p = v->ptr;
   p.offset = nir_imm_int(&c->b, (int)nir_src_as_uint(nir_src_for_ssa(v->ptr.offset)));
   return air_val_ptr(c, p);
}

""" + a)
    open(t, 'w').write(u)
i = sys.argv[1] + '/air_intrinsics.c'
u = open(i).read()
if 'b2l_select_vals' not in u:
    a = """   if (have_sampler) {
      if (!tex_handle(c, in, arg, AIR_PTR_SAMPLER, &samp))
         return false;
      arg++;
   }"""
    b = """   /* B2L (B1L-0084): a sampler chosen at run time: the operation with each, and the results selected */
   if (have_sampler && in->num_operands > arg) {
      struct air_val *sv = air_get(c, in->operands[arg]);
      if (sv && sv->kind == AIR_VAL_PTR_SELECT && sv->num_elems == 2) {
         const struct air_value *op = in->operands[arg];
         air_set(c, op, sv->elems[0]);
         bool ok = texture_op(c, in, n);
         struct air_val *ra = ok ? air_get(c, &in->base) : NULL;
         air_set(c, op, sv->elems[1]);
         ok = ok && texture_op(c, in, n);
         struct air_val *rb = ok ? air_get(c, &in->base) : NULL;
         air_set(c, op, sv);
         if (!ok)
            return false;
         struct air_val *r = b2l_select_vals(c, sv->def, ra, rb);
         if (!r)
            return air_error(c, "intrinsic: air.%s with a sampler chosen at run time, whose two results "
                                "do not have one shape", n->part[0]);
         air_set(c, &in->base, r);
         return true;
      }
   }
""" + a
    if u.count(a) != 1:
        sys.exit('air-b2l: texture_op sampler is not as expected (B1L-0084)')
    u = u.replace(a, b)
    a = """bool
air_translate_call(struct air_ctx *c, const struct air_inst *in, const char *name)"""
    if u.count(a) != 1:
        sys.exit('air-b2l: air_translate_call is not where B1L-0084 expects it')
    # the selector of two results of one shape, and texture_op's own declaration ahead of it
    helper = """/* B2L (B1L-0084): one value out of two of the same shape, by a condition */
static struct air_val *
b2l_select_vals(struct air_ctx *c, nir_def *cond, struct air_val *a, struct air_val *b)
{
   if (!a || !b)
      return a == b ? a : NULL;
   if (a->kind == AIR_VAL_SSA && b->kind == AIR_VAL_SSA && a->def && b->def &&
       a->def->num_components == b->def->num_components && a->def->bit_size == b->def->bit_size)
      return air_val_ssa(c, nir_bcsel(&c->b, cond, a->def, b->def));
   if (a->kind == AIR_VAL_AGG && b->kind == AIR_VAL_AGG && a->num_elems == b->num_elems) {
      struct air_val *r = rzalloc(c->mem_ctx, struct air_val);
      r->kind = AIR_VAL_AGG;
      r->num_elems = a->num_elems;
      r->elems = rzalloc_array(c->mem_ctx, struct air_val *, MAX2(a->num_elems, 1));
      for (unsigned k = 0; k < a->num_elems; k++) {
         if (!a->elems[k] && !b->elems[k])
            continue;
         if (!(r->elems[k] = b2l_select_vals(c, cond, a->elems[k], b->elems[k])))
            return NULL;
      }
      return r;
   }
   return NULL;
}

"""
    # texture_op is defined before air_translate_call; the helper must come before texture_op
    a2 = """static bool
texture_op(struct air_ctx *c, const struct air_inst *in, const struct air_name *n)"""
    if u.count(a2) == 1:
        u = u.replace(a2, helper + a2)
    else:
        a3 = "\ntexture_op(struct air_ctx *c, const struct air_inst *in, const struct air_name *n)\n{"
        k = u.find(a3)
        if k < 0:
            sys.exit('air-b2l: texture_op is not where B1L-0084 expects it')
        s0 = u.rfind('\n', 0, k)       # the line before: its return type
        u = u[:s0 + 1] + helper + u[s0 + 1:]
    open(i, 'w').write(u)
print('air-b2l: pointers chosen at run time passed to callees; samplers chosen at run time')

# B1L-0084 (M6b): Metal's max3/min3/median3 (air.fmax3, air.fast_fmax3, air.fmin3, air.fmedian3... in QuartzCore's
# main fragment shader). max3 = max(max(a, b), c); median3 = max(min(a, b), min(max(a, b), c)).
i = sys.argv[1] + '/air_intrinsics.c'
u = open(i).read()
if 'fmedian3' not in u:
    a = """   /* B2L (B1L-0084): a null texture, chosen where a function constant leaves a texture out */"""
    b = """   /* B2L (B1L-0084): three-operand max, min and median */
   {
      const char *op3 = name + 4;
      if (!strncmp(op3, "fast_", 5))
         op3 += 5;
      int which = !strncmp(op3, "fmax3.", 6) ? 1 : !strncmp(op3, "fmin3.", 6) ? 2 :
                  !strncmp(op3, "fmedian3.", 9) ? 3 : 0;
      if (!strncmp(name, "air.", 4) && which && in->num_operands >= 4) {
         nir_def *a3 = air_get_ssa(c, in->operands[1]);
         nir_def *b3 = air_get_ssa(c, in->operands[2]);
         nir_def *c3 = air_get_ssa(c, in->operands[3]);
         if (!a3 || !b3 || !c3)
            return false;
         nir_def *r;
         if (which == 1)
            r = nir_fmax(&c->b, nir_fmax(&c->b, a3, b3), c3);
         else if (which == 2)
            r = nir_fmin(&c->b, nir_fmin(&c->b, a3, b3), c3);
         else
            r = nir_fmax(&c->b, nir_fmin(&c->b, a3, b3), nir_fmin(&c->b, nir_fmax(&c->b, a3, b3), c3));
         air_set(c, &in->base, air_val_ssa(c, r));
         return true;
      }
   }
""" + a
    if u.count(a) != 1:
        sys.exit('air-b2l: the null texture branch is not where B1L-0084 expects it')
    u = u.replace(a, b)
    open(i, 'w').write(u)
print('air-b2l: fmax3, fmin3, fmedian3')

# B1L-0084 (M6b): a load through a null pointer reads 0. QuartzCore's main fragment shader picks null for a buffer a
# function constant says is absent (`select %fc, ptr null, ptr %19`) and loads through it only on the path the same
# constant rules out; with the choice now made while translating, that dead path still has to translate.
t = sys.argv[1] + '/air_to_nir.c'
u = open(t).read()
if 'B1L-0084): a null pointer reads 0' not in u:
    a = """   default:
      return air_error(c, "instruction: a load through a %s pointer",
                       ptr.class == AIR_PTR_NONE ? "null" : "texture or sampler"), NULL;
   }
   return as_bool ? nir_ine_imm(&c->b, res, 0) : res;"""
    b = """   case AIR_PTR_NONE:   /* B2L (B1L-0084): a null pointer reads 0, on a path its constant rules out */
      res = nir_imm_zero(&c->b, comps, bits);
      break;
   default:
      return air_error(c, "instruction: a load through a %s pointer",
                       ptr.class == AIR_PTR_NONE ? "null" : "texture or sampler"), NULL;
   }
   return as_bool ? nir_ine_imm(&c->b, res, 0) : res;"""
    if u.count(a) != 1:
        sys.exit('air-b2l: air_load_ptr is not as expected')
    u = u.replace(a, b)
    open(t, 'w').write(u)
print('air-b2l: loads through a null pointer read 0')

# B1L-0084 (M6b): an aggregate argument that holds nothing. QuartzCore passes a struct by value member by member, and
# its padding (`[8 x i8] undef`) arrives as one argument of array type, which a NIR parameter cannot be ("an array
# where a number belongs"): the variable blur fragments and, deeper, the main fragment shader. Such an argument takes
# no parameter; the callee gets the caller's empty value.
t = sys.argv[1] + '/air_to_nir.c'
u = open(t).read()
if 'b2l_val_empty' not in u:
    a = """/* B2L (B1L-0084): a resolved pointer whose offset is none or a constant, so the callee can have its own copy */"""
    b = """/* B2L (B1L-0084): a value with nothing in it: undef, or an aggregate of such */
static bool
b2l_val_empty(const struct air_val *v)
{
   if (!v || v->kind == AIR_VAL_NONE)
      return true;
   if (v->kind == AIR_VAL_SSA)
      return v->def && v->def->parent_instr->type == nir_instr_type_undef;
   if (v->kind == AIR_VAL_AGG) {
      for (unsigned k = 0; k < v->num_elems; k++)
         if (!b2l_val_empty(v->elems[k]))
            return false;
      return true;
   }
   return false;
}

""" + a
    if u.count(a) != 1:
        sys.exit('air-b2l: the B1L-0084 pointer helpers are not where b2l_val_empty goes')
    u = u.replace(a, b)
    a = """      if (argv && argv[i] && """
    k = u.find(a + "argv[i]->kind == AIR_VAL_PTR_SELECT && argv[i]->num_elems == 2 &&")
    if k < 0:
        sys.exit('air-b2l: the select-argument test is not where b2l_val_empty goes')
    u = u[:k] + """      {   /* B2L (B1L-0084): an aggregate argument holding nothing takes no parameter */
         const struct air_type *at0 = f->args[i].type;
         if (argv && at0 && (at0->kind == AIR_TYPE_ARRAY || at0->kind == AIR_TYPE_STRUCT) &&
             b2l_val_empty(argv[i])) {
            is_param[i] = false;
            specialised = true;
            continue;
         }
      }
""" + u[k:]
    open(t, 'w').write(u)
print('air-b2l: empty aggregate arguments')

# B1L-0084 (M6b): an aggregate argument with values in it (QuartzCore's main fragment shader passes
# `[2 x FragmentShaderConfig::Image]` by value). Its numeric leaves become parameters, in a depth-first order the
# parameters, the callee's rebuilt aggregate and the call all share; a leaf that is not a number (a pointer) keeps
# the refusal.
t = sys.argv[1] + '/air_to_nir.c'
u = open(t).read()
if 'b2l_leaves' not in u:
    a = """/* B2L (B1L-0084): a value with nothing in it: undef, or an aggregate of such */"""
    b = """/* B2L (B1L-0084): the numeric leaves of an aggregate, depth first; ~0u if one is not a number */
static unsigned
b2l_leaves(const struct air_val *v, nir_def **out, unsigned n)
{
   if (!v || v->kind == AIR_VAL_NONE)
      return n;
   if (v->kind == AIR_VAL_SSA) {
      if (out)
         out[n] = v->def;
      return n + 1;
   }
   if (v->kind != AIR_VAL_AGG)
      return ~0u;
   for (unsigned k = 0; k < v->num_elems && n != ~0u; k++)
      n = b2l_leaves(v->elems[k], out, n);
   return n;
}

/* the same aggregate in the callee, its leaves the parameters from *pidx on */
static struct air_val *
b2l_rebuild(struct air_ctx *c, const struct air_val *v, unsigned *pidx)
{
   if (!v || v->kind == AIR_VAL_NONE)
      return (struct air_val *)v;
   if (v->kind == AIR_VAL_SSA)
      return air_val_ssa(c, nir_load_param(&c->b, (*pidx)++));
   struct air_val *r = air_val_agg(c, v->num_elems);
   for (unsigned k = 0; k < v->num_elems; k++)
      r->elems[k] = b2l_rebuild(c, v->elems[k], pidx);
   return r;
}

""" + a
    if u.count(a) != 1:
        sys.exit('air-b2l: b2l_val_empty is not where the leaf helpers go')
    u = u.replace(a, b)
    reps = [
    ("""   bool specialised = false;
   for (unsigned i = 0; i < f->num_args; i++) {""",
     """   bool specialised = false;
   unsigned b2l_extra = 0;   /* B2L (B1L-0084): the leaves of aggregate arguments */
   for (unsigned i = 0; i < f->num_args; i++) {"""),
    ("""            is_param[i] = false;
            specialised = true;
            continue;
         }
      }""",
     """            is_param[i] = false;
            specialised = true;
            continue;
         }
         unsigned nl;
         if (argv && argv[i] && argv[i]->kind == AIR_VAL_AGG && at0 &&
             (at0->kind == AIR_TYPE_ARRAY || at0->kind == AIR_TYPE_STRUCT) &&
             (nl = b2l_leaves(argv[i], NULL, 0)) != ~0u) {
            is_param[i] = true;
            specialised = true;
            b2l_extra += nl;
            continue;
         }
      }"""),
    ("""   unsigned nparams = 3 * f->num_args + nret;   /* B2L (B1L-0084): a chosen pointer takes three */""",
     """   unsigned nparams = 3 * f->num_args + nret + b2l_extra;   /* B2L (B1L-0084): a chosen pointer takes three */"""),
    ("""      if (argv && argv[i] && argv[i]->kind == AIR_VAL_PTR_SELECT) {   /* B2L (B1L-0084): condition, two offsets */
         for (unsigned q = 0; q < 3; q++)""",
     """      if (argv && argv[i] && argv[i]->kind == AIR_VAL_AGG) {   /* B2L (B1L-0084): one parameter a leaf */
         nir_def *lv[256];
         unsigned nl = b2l_leaves(argv[i], NULL, 0);
         if (nl > 256) {
            air_error(c, "intrinsic: an aggregate argument of %u leaves", nl);
            return NULL;
         }
         b2l_leaves(argv[i], lv, 0);
         for (unsigned q = 0; q < nl; q++)
            fn->params[idx++] = (nir_parameter){ .num_components = (uint8_t)lv[q]->num_components,
                                                 .bit_size = (uint8_t)lv[q]->bit_size };
         continue;
      }
      if (argv && argv[i] && argv[i]->kind == AIR_VAL_PTR_SELECT) {   /* B2L (B1L-0084): condition, two offsets */
         for (unsigned q = 0; q < 3; q++)"""),
    ("""      if (argv && argv[i] && argv[i]->kind == AIR_VAL_PTR_SELECT) {   /* B2L (B1L-0084): the condition is a parameter */""",
     """      if (argv && argv[i] && argv[i]->kind == AIR_VAL_AGG) {   /* B2L (B1L-0084): the aggregate, rebuilt */
         air_set(c, &f->args[i], b2l_rebuild(c, argv[i], &pidx));
         continue;
      }
      if (argv && argv[i] && argv[i]->kind == AIR_VAL_PTR_SELECT) {   /* B2L (B1L-0084): the condition is a parameter */"""),
    ("""      if (argv[i] && argv[i]->kind == AIR_VAL_PTR_SELECT) {   /* B2L (B1L-0084): condition, two offsets */
         call->params[idx++] = nir_src_for_ssa(nir_b2i32(&c->b, argv[i]->def));""",
     """      if (argv[i] && argv[i]->kind == AIR_VAL_AGG) {   /* B2L (B1L-0084): the aggregate's leaves */
         nir_def *lv[256];
         unsigned nl = b2l_leaves(argv[i], lv, 0);
         for (unsigned q = 0; q < nl; q++)
            call->params[idx++] = nir_src_for_ssa(lv[q]);
         continue;
      }
      if (argv[i] && argv[i]->kind == AIR_VAL_PTR_SELECT) {   /* B2L (B1L-0084): condition, two offsets */
         call->params[idx++] = nir_src_for_ssa(nir_b2i32(&c->b, argv[i]->def));"""),
    ]
    for a, b in reps:
        if u.count(a) != 1:
            sys.exit('air-b2l: B1L-0084 aggregate-argument patch: text not as expected: ' + a[:70])
        u = u.replace(a, b)
    open(t, 'w').write(u)
print('air-b2l: aggregate arguments, leaf by leaf')

# B1L-0085 (M6b): a fragment shader that stores to [[depth]] in a pipeline with no depth attachment. Metal drops the
# store; the library refused the pipeline, and QuartzCore's TimgA2Xhf_Isrc (built with every function constant 0, see
# B1L-0085's notes) made backboardd exit (B1L-0084). The stores to FRAG_RESULT_DEPTH are removed instead.
p = sys.argv[1] + '/air_metal.c'
s = open(p).read()
if 'b2l_drop_depth_writes' not in s:
    a = """static bool
vertex_reads_attributes(nir_shader *vs)
{"""
    b = """/* B2L (B1L-0085): what Metal does with a [[depth]] store and no depth attachment: nothing */
static void
b2l_drop_depth_writes(nir_shader *fs)
{
   nir_foreach_function_impl(impl, fs) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;
            nir_intrinsic_instr *in = nir_instr_as_intrinsic(instr);
            if (in->intrinsic != nir_intrinsic_store_deref)
               continue;
            nir_deref_instr *d = nir_src_as_deref(in->src[0]);
            if (d && d->deref_type == nir_deref_type_var &&
                d->var->data.location == FRAG_RESULT_DEPTH)
               nir_instr_remove(instr);
         }
      }
      nir_metadata_preserve(impl, nir_metadata_block_index | nir_metadata_dominance);
   }
   fs->info.outputs_written &= ~BITFIELD64_BIT(FRAG_RESULT_DEPTH);
}

""" + a
    if s.count(a) != 1:
        sys.exit('air-b2l: vertex_reads_attributes is not where B1L-0085 expects it')
    s = s.replace(a, b)
    a = """   if (out.fragment_writes_depth && !shape->depth_format) {
      say(why, why_size, "%s stores to [[depth]] and this pipeline has no depth attachment, "
                         "so there is nowhere for that store to go",
          render->finfo.entry_point ? render->finfo.entry_point : "the fragment function");
      goto fail;
   }"""
    b = """   if (out.fragment_writes_depth && !shape->depth_format) {   /* B2L (B1L-0085): dropped, as Metal does */
      b2l_drop_depth_writes(fs);
      out.fragment_writes_depth = 0;
   }"""
    if s.count(a) != 1:
        sys.exit('air-b2l: the depth refusal is not as expected')
    s = s.replace(a, b)
    open(p, 'w').write(s)
print('air-b2l: depth stores without a depth attachment dropped')

# B1L-0085 (M6b): which fragment shaders backboardd builds, and how big they are -- a Mali job stopped with
# INSTR_INVALID_ENC (0x51) in B1L-0084 and the log did not say whose. One kernel log line a pipeline (first 400):
# "air-b2l: render pipeline <fragment entry>: <n> NIR instructions".
p = sys.argv[1] + '/air_metal.c'
s = open(p).read()
if 'b2l_say_pipeline' not in s:
    a = '#include "util/u_inlines.h"\n'
    if s.count(a) != 1:
        sys.exit('air-b2l: air_metal.c includes are not as expected (B1L-0085)')
    s = s.replace(a, a + '#include <sys/sysctl.h>\n')
    a = """/* B2L (B1L-0085): what Metal does with a [[depth]] store and no depth attachment: nothing */"""
    b = """/* B2L (B1L-0085): one line in the kernel's log for a render pipeline built */
static void
b2l_say_pipeline(const char *entry, nir_shader *fs)
{
   static unsigned said;
   if (said >= 400)
      return;
   said++;
   unsigned n = 0;
   nir_foreach_function_impl(impl, fs)
      nir_foreach_block(block, impl)
         nir_foreach_instr(instr, block)
            n++;
   char line[256];
   int len = snprintf(line, sizeof(line), "air-b2l: render pipeline %s: %u NIR instructions",
                      entry ? entry : "?", n);
   if (len > 0)
      (void)sysctlbyname("kern.hakios.say", NULL, NULL, line, (size_t)MIN2(len, (int)sizeof(line) - 1));
}

""" + a
    if s.count(a) != 1:
        sys.exit('air-b2l: b2l_drop_depth_writes is not where B1L-0085b expects it')
    s = s.replace(a, b)
    a = """   slot_shapes(fs, render->frag_slots, render->frag_compare_sampler);"""
    if s.count(a) != 1:
        sys.exit('air-b2l: slot_shapes(fs) is not as expected')
    s = s.replace(a, "   b2l_say_pipeline(render->finfo.entry_point, fs);   /* B2L (B1L-0085) */\n" + a)
    open(p, 'w').write(s)
print('air-b2l: render pipelines said in the kernel log')

# B1L-0086 (M6b): the specialised AIR modules backboardd builds pipelines from, written to /b2l-dump on SYSTEM
# (made 01777 by the kernel) -- f-<entry>.air and v-<entry>.air, once a name, 160 files at most -- so that they can be
# translated and compiled on the Mac (they come back with the next SYSTEM flash, which reads the changed blocks).
# A Mali job stopped on INSTR_INVALID_ENC (0x51) in B1L-0084 and 0085 and the log cannot say whose shader it was.
p = sys.argv[1] + '/air_metal.c'
s = open(p).read()
if 'b2l_dump_module' not in s:
    a = '#include <sys/sysctl.h>\n'
    if s.count(a) != 1:
        sys.exit('air-b2l: air_metal.c has no sys/sysctl.h (B1L-0086 follows B1L-0085)')
    s = s.replace(a, a + '#include <fcntl.h>\n#include <unistd.h>\n')
    a = """/* B2L (B1L-0085): one line in the kernel's log for a render pipeline built */"""
    b = """/* B2L (B1L-0086/0087): a module of backboardd's, kept for the Mac. /b2l-dump turned out to be on USERDATA (the root
 * is rd=b2l-userdata), which the Mac cannot read back; so the module goes through the kernel's log instead, as
 * base64 in 192-character chunks, each with its own checksum, the whole module twice (the log can interleave lines
 * written at once from several cores): "B2LMOD <kind>-<name> <i>/<n> <fnv32> <chunk>". Once a name, 40 at most. */
static void
b2l_dump_module(const char *kind, const char *entry, const void *bitcode, size_t size)
{
   static int in_bbd = -1;
   static unsigned modules;
   static char seen[40][96];
   static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
   if (in_bbd < 0)
      in_bbd = getprogname() && !strcmp(getprogname(), "backboardd");
   if (!in_bbd || !bitcode || !size || modules >= 40 || size > 400000)
      return;
   char name[96];
   snprintf(name, sizeof(name), "%s-%.80s", kind, entry && entry[0] ? entry : "unnamed");
   for (char *q = name; *q; q++)
      if (*q == ' ' || *q == '/')
         *q = '_';
   for (unsigned k = 0; k < modules; k++)
      if (!strcmp(seen[k], name))
         return;
   snprintf(seen[modules++], sizeof(seen[0]), "%s", name);
   const unsigned char *in = bitcode;
   size_t enc_len = 4 * ((size + 2) / 3);
   char *enc = malloc(enc_len + 1);
   if (!enc)
      return;
   size_t o = 0;
   for (size_t i = 0; i < size; i += 3) {
      unsigned v = in[i] << 16 | (i + 1 < size ? in[i + 1] << 8 : 0) | (i + 2 < size ? in[i + 2] : 0);
      enc[o++] = b64[(v >> 18) & 63];
      enc[o++] = b64[(v >> 12) & 63];
      enc[o++] = i + 1 < size ? b64[(v >> 6) & 63] : '=';
      enc[o++] = i + 2 < size ? b64[v & 63] : '=';
   }
   enc[o] = 0;
   unsigned chunks = (unsigned)((o + 191) / 192);
   for (int copy = 0; copy < 2; copy++) {
      for (unsigned c = 0; c < chunks; c++) {
         const char *part = enc + (size_t)c * 192;
         size_t len = MIN2(192, o - (size_t)c * 192);
         uint32_t h = 2166136261u;
         for (size_t k = 0; k < len; k++)
            h = (h ^ (unsigned char)part[k]) * 16777619u;
         char line[400];
         int n = snprintf(line, sizeof(line), "B2LMOD %s %u/%u %08x %.*s", name, c, chunks, h, (int)len, part);
         if (n > 0)
            (void)sysctlbyname("kern.hakios.say", NULL, NULL, line, (size_t)MIN2(n, (int)sizeof(line) - 1));
         if ((c & 31) == 31)
            usleep(2000);   /* a breath for the log's writer */
      }
   }
   free(enc);
}

""" + a
    if s.count(a) != 1:
        sys.exit('air-b2l: b2l_say_pipeline is not where B1L-0086 expects it')
    s = s.replace(a, b)
    a = """   b2l_say_pipeline(render->finfo.entry_point, fs);   /* B2L (B1L-0085) */"""
    if s.count(a) != 1:
        sys.exit('air-b2l: the B1L-0085 pipeline line is not as expected')
    s = s.replace(a, a + """
   b2l_dump_module("f", render->finfo.entry_point, fragment_bitcode, fragment_size);   /* B2L (B1L-0086) */
   b2l_dump_module("v", vertex_entry ? vertex_entry : render->finfo.entry_point, vertex_bitcode, vertex_size);""")
    open(p, 'w').write(s)
print('air-b2l: backboardd modules dumped to /b2l-dump')

# B1L-0088 (M6b): panfrost's noperspective lowering (pan_lower_noperspective.c) multiplies a varying by the 32-bit
# position w, on both sides, whatever the varying's size. Metal marks varyings no_perspective all the time and
# QuartzCore's _lph shaders carry them as half: an fmul of 16-bit by 32-bit bits, which a release build of NIR does
# not check, and Bifrost emits an instruction the Mali stops on (INSTR_INVALID_ENC, 0x51: the staircase pictures of
# B1L-0084..0087). Found with b2l-bicomp (assertions on) over the modules backboardd sent. w is converted to the
# varying's size first.
q = sys.argv[1] + '/../../panfrost/util/pan_lower_noperspective.c'
u = open(q).read()
if 'B1L-0088' not in u:
    a = """   nir_def *old_value = intrin->src[0].ssa;
   nir_def *noperspective_value = nir_fmul(b, old_value, state->pos_w);"""
    b = """   nir_def *old_value = intrin->src[0].ssa;
   /* B2L (B1L-0088): w at the varying's own size (a half varying times a float w was an invalid instruction) */
   nir_def *noperspective_value =
      nir_fmul(b, old_value, nir_f2fN(b, state->pos_w, old_value->bit_size));"""
    if u.count(a) != 1:
        sys.exit('air-b2l: pan_lower_noperspective.c (vertex side) is not as expected')
    u = u.replace(a, b)
    a = """   nir_def *new_value = nir_fmul(b, &intrin->def, fragcoord_w);"""
    b = """   /* B2L (B1L-0088): fragcoord.w at the varying's own size */
   nir_def *new_value = nir_fmul(b, &intrin->def, nir_f2fN(b, fragcoord_w, intrin->def.bit_size));"""
    if u.count(a) != 1:
        sys.exit('air-b2l: pan_lower_noperspective.c (fragment side) is not as expected')
    u = u.replace(a, b)
    open(q, 'w').write(u)
print('air-b2l: noperspective lowering at the varying size')

# B1L-0089 (M6b): Mesa's assertions, when the build has them (B2L_MESA_ASSERTS=1 in build-mesa.sh), reported instead
# of fatal: libhakiosair defines __assert_rtn, which every assert() of the image reaches (the static linker binds a
# reference to a symbol the image defines itself), writes "mesa-assert: <expr> (<file>:<line>, <function>)" to the
# kernel's log -- 600 at most -- and returns. panfrost compiles at pipeline creation, so an assertion lands right
# after the "air-b2l: render pipeline <name>" line of the shader that broke it: the 0x51 faults left (99 in B1L-0088)
# are in shaders b2l-bicomp cannot compile on the Mac without the driver's own lowering.
p = sys.argv[1] + '/air_metal.c'
s = open(p).read()
if 'mesa-assert:' not in s:
    a = """/* B2L (B1L-0085): one line in the kernel's log for a render pipeline built */"""
    b = """/* B2L (B1L-0089): a failed assertion, said and survived */
void __assert_rtn(const char *func, const char *file, int line, const char *expr);
void
__assert_rtn(const char *func, const char *file, int line, const char *expr)
{
   static unsigned said;
   if (said >= 600)
      return;
   said++;
   const char *f = file ? strrchr(file, '/') : NULL;
   char msg[400];
   int n = snprintf(msg, sizeof(msg), "mesa-assert: %.200s (%s:%d, %s)", expr ? expr : "?",
                    f ? f + 1 : (file ? file : "?"), line, func ? func : "?");
   if (n > 0)
      (void)sysctlbyname("kern.hakios.say", NULL, NULL, msg, (size_t)MIN2(n, (int)sizeof(msg) - 1));
}

/* the assertions without NIR's whole-shader validation after every pass, which on QuartzCore's main fragment
 * shader (75 592 instructions) would hold backboardd for minutes; a caller's own NIR_DEBUG wins */
__attribute__((constructor)) static void
b2l_nir_debug_default(void)
{
   setenv("NIR_DEBUG", "novalidate", 0);
}

""" + a
    if s.count(a) != 1:
        sys.exit('air-b2l: b2l_say_pipeline is not where B1L-0089 expects it')
    s = s.replace(a, b)
    open(p, 'w').write(s)
print('air-b2l: assertions reported to the kernel log')

# B1L-0090 (M6b): two rules of Bifrost v7 (the Mali-G76) that AIR breaks and a release build of Mesa does not check
# -- found by B1L-0089's assertions in backboardd's real compile path:
#  1. a vertex shader stores varyings in 32 bits only ("T_size == 32 || (arch >= 9 && T_size == 16)",
#     bi_emit_store_vary). GL front ends always store 32; AIR's _lph shaders store half. Before v9 a 16-bit
#     store_output of a vertex shader is widened to 32 (value and src_type), the fragment side keeps reading half
#     (LD_VAR converts).
#  2. load_raw_vertex_offset_pan reaching the backend (bi_emit_intrinsic: unreachable): panfrost's sysval lowering
#     rewrites its uses but leaves it for DCE. An instruction with no use is given 0 instead of undefined behaviour.
q = sys.argv[1] + '/../../panfrost/compiler/bifrost_compile.c'
u = open(q).read()
if 'B1L-0090' not in u:
    a = """void
bifrost_preprocess_nir(nir_shader *nir, unsigned gpu_id)
{"""
    b = """/* B2L (B1L-0090): a 16-bit varying store of a vertex shader, widened to 32 bits for Bifrost before v9 */
static bool
b2l_widen_vs_output(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   (void)data;
   if (intr->intrinsic != nir_intrinsic_store_output || intr->src[0].ssa->bit_size != 16)
      return false;
   nir_alu_type t = nir_alu_type_get_base_type(nir_intrinsic_src_type(intr));
   b->cursor = nir_before_instr(&intr->instr);
   nir_def *v = intr->src[0].ssa;
   nir_def *w = t == nir_type_float ? nir_f2f32(b, v) : t == nir_type_int ? nir_i2i32(b, v) : nir_u2u32(b, v);
   nir_src_rewrite(&intr->src[0], w);
   nir_intrinsic_set_src_type(intr, t | 32);
   return true;
}

""" + a
    if u.count(a) != 1:
        sys.exit('air-b2l: bifrost_preprocess_nir is not as expected (B1L-0090)')
    u = u.replace(a, b)
    a = """      if (gpu_id >= 0x9000) {
         NIR_PASS(_, nir, nir_lower_mediump_io, nir_var_shader_out,
                  BITFIELD64_BIT(VARYING_SLOT_PSIZ), false);
      }
"""
    b = """      if (gpu_id >= 0x9000) {
         NIR_PASS(_, nir, nir_lower_mediump_io, nir_var_shader_out,
                  BITFIELD64_BIT(VARYING_SLOT_PSIZ), false);
      } else {
         NIR_PASS(_, nir, nir_shader_intrinsics_pass, b2l_widen_vs_output,
                  nir_metadata_control_flow, NULL);   /* B2L (B1L-0090) */
      }
"""
    if u.count(a) != 1:
        sys.exit('air-b2l: the vertex mediump branch is not as expected (B1L-0090)')
    u = u.replace(a, b)
    a = """   case nir_intrinsic_load_raw_vertex_id_pan:
      assert(!b->shader->malloc_idvs);
      bi_mov_i32_to(b, dst, bi_vertex_id(b));
      break;"""
    b = a + """

   case nir_intrinsic_load_raw_vertex_offset_pan:   /* B2L (B1L-0090): left behind by the sysval lowering */
      bi_mov_i32_to(b, dst, bi_imm_u32(0));
      break;"""
    if u.count(a) != 1:
        sys.exit('air-b2l: load_raw_vertex_id_pan is not as expected (B1L-0090)')
    u = u.replace(a, b)
    open(q, 'w').write(u)
print('air-b2l: Bifrost v7 vertex varyings widened to 32 bits; raw vertex offset handled')

# B1L-0090 (M6b): panfrost's sysval lowering rewrites an intrinsic's uses to a load from the sysval UBO and leaves the
# intrinsic for DCE. One that DCE keeps reaches the Bifrost backend: load_raw_vertex_offset_pan (unreachable there)
# and load_ssbo_address, for which the backend emits LEA_BUFFER -- a Valhall (v9) instruction the Mali-G76 cannot
# decode, under an assertion (arch >= 9) a release build drops: an INSTR_INVALID_ENC candidate. The replaced
# instruction is removed at once.
q = sys.argv[1] + '/../../gallium/drivers/panfrost/pan_nir_lower_sysvals.c'
u = open(q).read()
if 'B1L-0090' not in u:
    a = """   nir_def_rewrite_uses(old, val);
   return true;
}"""
    b = """   nir_def_rewrite_uses(old, val);
   nir_instr_remove(instr);   /* B2L (B1L-0090): not left for a DCE that may keep it */
   return true;
}"""
    if u.count(a) != 1:
        sys.exit('air-b2l: pan_nir_lower_sysvals.c is not as expected (B1L-0090)')
    u = u.replace(a, b)
    open(q, 'w').write(u)
print('air-b2l: sysval lowering removes what it replaces')

# B1L-0091 (M6b): AIR's discard_fragment is translated as NIR demote (air_intrinsics.c), which the Bifrost backend
# does not handle (bi_emit_intrinsic: unreachable -- B1L-0090's assertions aborted backboardd on it, in the main
# fragment shader). Bifrost's DISCARD already keeps helper lanes alive while a derivative still needs them
# (bi_helper_invocations.c), which is demote's contract: demote and demote_if are emitted as terminate.
q = sys.argv[1] + '/../../panfrost/compiler/bifrost_compile.c'
u = open(q).read()
if 'B1L-0091' not in u:
    a = """   case nir_intrinsic_terminate_if:
      bi_discard_b32(b, bi_src_index(&instr->src[0]));
      break;

   case nir_intrinsic_terminate:
"""
    b = """   case nir_intrinsic_demote_if:   /* B2L (B1L-0091): AIR's discard_fragment */
   case nir_intrinsic_terminate_if:
      bi_discard_b32(b, bi_src_index(&instr->src[0]));
      break;

   case nir_intrinsic_demote:      /* B2L (B1L-0091) */
   case nir_intrinsic_terminate:
"""
    if u.count(a) != 1:
        sys.exit('air-b2l: Bifrost terminate is not as expected (B1L-0091)')
    u = u.replace(a, b)
    open(q, 'w').write(u)
print('air-b2l: Bifrost emits demote as discard')

# B1L-0092 (M6b): a kernel's threadgroup buffer sized at dispatch (Metal's -setThreadgroupMemoryLength:atIndex:).
# QuartzCore's compute_average_luma (the luminance Liquid Glass adapts its tint to) takes one, and every dispatch of
# it was refused (233 in B1L-0091's boot). The size comes from the driver through rwa_set_threadgroup_memory; the
# buffer's base is 0 in shared memory (air_to_nir), so a kernel that also has static threadgroup arrays is still
# refused. The compute state is rebuilt when the size changes, with shared_size raised to it.
q = sys.argv[1] + '/air_metal.c'
u = open(q).read()
if 'B1L-0092' not in u:
    def rep(a, b, what):
        global u
        if u.count(a) != 1:
            sys.exit('air-b2l: %s is not as expected (B1L-0092)' % what)
        u = u.replace(a, b)
    rep("""   bool dynamic_threadgroup;      /* a threadgroup buffer sized by the dispatch */
""", """   bool dynamic_threadgroup;      /* a threadgroup buffer sized by the dispatch */
   unsigned b2l_tg_bytes;         /* B2L (B1L-0092): its size, from the driver */
""", 'struct rwa_compute')
    rep("""   void *cso;
   unsigned cso_block[3];
};""", """   void *cso;
   unsigned cso_block[3];
   unsigned cso_tg_bytes;         /* B2L (B1L-0092) */
};""", 'the compute state cache')
    rep("""   if (compute->cso && compute->cso_block[0] == block[0] &&
       compute->cso_block[1] == block[1] && compute->cso_block[2] == block[2])
      return compute->cso;""", """   if (compute->cso && compute->cso_block[0] == block[0] &&
       compute->cso_block[1] == block[1] && compute->cso_block[2] == block[2] &&
       compute->cso_tg_bytes == compute->b2l_tg_bytes)
      return compute->cso;""", 'compute_state_for')
    rep("""   cs->info.workgroup_size_variable = false;

   finalize_for_gallium(cs, device->screen, device->ubo_base);

   void *cso = device->pipe->create_compute_state(""", """   cs->info.workgroup_size_variable = false;
   if (compute->dynamic_threadgroup)   /* B2L (B1L-0092) */
      cs->info.shared_size = MAX2(cs->info.shared_size, compute->b2l_tg_bytes);

   finalize_for_gallium(cs, device->screen, device->ubo_base);

   void *cso = device->pipe->create_compute_state(""", 'the compute state build')
    rep("""   compute->cso = cso;
   compute->cso_block[0] = block[0];""", """   compute->cso = cso;
   compute->cso_tg_bytes = compute->b2l_tg_bytes;   /* B2L (B1L-0092) */
   compute->cso_block[0] = block[0];""", 'the compute state record')
    rep("""   if (compute->dynamic_threadgroup) {
      say(why, why_size, "%s takes a threadgroup buffer whose size only the dispatch can "
                         "give (Metal's -setThreadgroupMemoryLength:atIndex:), and this "
                         "call has no argument for it", name);
      return -1;
   }""", """   if (compute->dynamic_threadgroup &&
       (compute->b2l_tg_bytes == 0 || compute->info.threadgroup_bytes != 0)) {   /* B2L (B1L-0092) */
      say(why, why_size, "%s takes a threadgroup buffer whose size only the dispatch can "
                         "give (Metal's -setThreadgroupMemoryLength:atIndex:), and %s", name,
          compute->b2l_tg_bytes == 0 ? "none was given"
                                     : "it also has static threadgroup arrays at the same base");
      return -1;
   }""", 'the threadgroup refusal')
    rep("""int
rwa_dispatch(struct rwa_compute *compute, const struct rwa_bindings *bindings,""", """/* B2L (B1L-0092): the size of the kernel's threadgroup buffer for the next dispatches */
void rwa_set_threadgroup_memory(struct rwa_compute *compute, unsigned bytes);
void
rwa_set_threadgroup_memory(struct rwa_compute *compute, unsigned bytes)
{
   compute->b2l_tg_bytes = bytes;
}

int
rwa_dispatch(struct rwa_compute *compute, const struct rwa_bindings *bindings,""", 'rwa_dispatch')
    open(q, 'w').write(u)
print('air-b2l: threadgroup buffers sized at dispatch')

# B1L-0093 (M6b): which shaders the Mali stopped on. B1L-0091 showed 112 fragment jobs ending in INSTR_INVALID_ENC
# (0x51), but the kernel's queue knows only GPU addresses. Each batch now keeps the names of the shaders it draws with
# (the AIR function names air_to_nir gives NIR), and a blend shader is flagged; panfrost remembers the last 512 chains
# it submitted with their batch's names, and after each submit reads kern.hakios.mali_failures (the kernel's last 32
# failed chains, B1L-0093) and says each new failure with the names: "b2l-batch: ... failed ...: NAMES".
P = sys.argv[1] + '/../../gallium/drivers/panfrost/'
u = open(P + 'pan_job.h').read()
if 'B1L-0093' not in u:
    a = """   /* Set when cycle count is required for this batch. */
   bool need_job_req_cycle_count;
"""
    b = a + """
   /* B2L (B1L-0093): the names of the shaders this batch draws with, for the kernel log */
   char b2l_label[320];
"""
    if u.count(a) != 1:
        sys.exit('air-b2l: struct panfrost_batch is not as expected (B1L-0093)')
    u = u.replace(a, b)
    a = "#endif"
    i = u.rindex(a)
    u = u[:i] + """#include <string.h>
/* B2L (B1L-0093): a shader's name added to its batch's label, once */
static inline void
b2l_batch_label(struct panfrost_batch *batch, const char *name)
{
   if (!name || !name[0])
      return;
   char *l = batch->b2l_label;
   size_t len = strlen(l), n = strlen(name);
   for (const char *p = l; (p = strstr(p, name)) != NULL; p += n) {
      if ((p == l || p[-1] == ',') && (p[n] == ',' || p[n] == 0))
         return;
   }
   if (len + n + 2 >= sizeof(batch->b2l_label)) {
      if (len + 2 < sizeof(batch->b2l_label) && (len == 0 || l[len - 1] != '+'))
         strcpy(l + len, len ? ",+" : "+");
      return;
   }
   if (len)
      l[len++] = ',';
   memcpy(l + len, name, n + 1);
}

""" + u[i:]
    open(P + 'pan_job.h', 'w').write(u)

u = open(P + 'pan_jm.c').read()
if 'B1L-0093' not in u:
    def rep(a, b, what):
        global u
        if u.count(a) != 1:
            sys.exit('air-b2l: pan_jm.c %s is not as expected (B1L-0093)' % what)
        u = u.replace(a, b)
    rep('#include "pan_job.h"\n', '''#include "pan_job.h"
#include <pthread.h>
#include <sys/sysctl.h>

/* B2L (B1L-0093): the chains this process submitted, with their batch's shader names, and the kernel's failures */
struct b2l_mali_failures {
   uint32_t count, pad;
   struct { uint64_t seq, chain; uint32_t status, fragment; } e[32];
};
static struct { uint64_t chain; bool fragment; char label[320]; } b2l_chains[512];
static unsigned b2l_chain_next;
static uint32_t b2l_failures_seen;
static bool b2l_failures_started;
static pthread_mutex_t b2l_chain_lock = PTHREAD_MUTEX_INITIALIZER;

static void
b2l_chain_note(uint64_t chain, bool fragment, const char *label)
{
   pthread_mutex_lock(&b2l_chain_lock);
   unsigned k = b2l_chain_next++ % 512;
   b2l_chains[k].chain = chain;
   b2l_chains[k].fragment = fragment;
   snprintf(b2l_chains[k].label, sizeof(b2l_chains[k].label), "%s", label[0] ? label : "(no shader named)");
   pthread_mutex_unlock(&b2l_chain_lock);
}

static void
b2l_chain_failures(void)
{
   struct b2l_mali_failures f;
   size_t len = sizeof(f);
   if (sysctlbyname("kern.hakios.mali_failures", &f, &len, NULL, 0) != 0 || len != sizeof(f))
      return;
   pthread_mutex_lock(&b2l_chain_lock);
   if (!b2l_failures_started) {   /* failures from before this process are not ours */
      b2l_failures_started = true;
      b2l_failures_seen = f.count;
   }
   uint32_t from = f.count - b2l_failures_seen > 32 ? f.count - 32 : b2l_failures_seen;
   for (uint32_t i = from; i < f.count; i++) {
      const char *label = "(not a chain of this process's last 512)";
      for (unsigned back = 1; back <= 512 && back <= b2l_chain_next; back++) {
         unsigned k = (b2l_chain_next - back) % 512;
         if (b2l_chains[k].chain == f.e[i % 32].chain) {
            label = b2l_chains[k].label;
            break;
         }
      }
      char line[440];
      snprintf(line, sizeof(line), "b2l-batch: job %llu (%s chain %#llx) failed with status %#x: %s",
               (unsigned long long)f.e[i % 32].seq, f.e[i % 32].fragment ? "fragment" : "vertex/tiler/compute",
               (unsigned long long)f.e[i % 32].chain, f.e[i % 32].status, label);
      (void)sysctlbyname("kern.hakios.say", NULL, NULL, line, strlen(line));
   }
   b2l_failures_seen = f.count;
   pthread_mutex_unlock(&b2l_chain_lock);
}
''', 'includes')
    rep('''   submit.bo_handles = (uint64_t)(uintptr_t)bo_handles;
   if (ctx->is_noop)''', '''   submit.bo_handles = (uint64_t)(uintptr_t)bo_handles;
   b2l_chain_failures();   /* B2L (B1L-0093): failures of earlier chains, said with their names */
   b2l_chain_note(first_job_desc, (reqs & PANFROST_JD_REQ_FS) != 0, batch->b2l_label);
   if (ctx->is_noop)''', 'the submit')
    rep('''   struct panfrost_compiled_shader *vs = ctx->prog[PIPE_SHADER_VERTEX];
   bool secondary_shader = vs->info.vs.secondary_enable;
   bool idvs = vs->info.vs.idvs;
''', '''   struct panfrost_compiled_shader *vs = ctx->prog[PIPE_SHADER_VERTEX];
   bool secondary_shader = vs->info.vs.secondary_enable;
   bool idvs = vs->info.vs.idvs;

   /* B2L (B1L-0093) */
   for (unsigned st = PIPE_SHADER_VERTEX; st <= PIPE_SHADER_FRAGMENT; st += PIPE_SHADER_FRAGMENT - PIPE_SHADER_VERTEX) {
      const struct panfrost_uncompiled_shader *us = ctx->uncompiled[st];
      if (us && us->nir && us->nir->info.name)
         b2l_batch_label(batch, us->nir->info.name);
   }
''', 'jm_launch_draw')
    rep('''GENX(jm_launch_grid)(struct panfrost_batch *batch,
                     const struct pipe_grid_info *info)
{
''', '''GENX(jm_launch_grid)(struct panfrost_batch *batch,
                     const struct pipe_grid_info *info)
{
   {   /* B2L (B1L-0093) */
      const struct panfrost_uncompiled_shader *us = batch->ctx->uncompiled[PIPE_SHADER_COMPUTE];
      if (us && us->nir && us->nir->info.name)
         b2l_batch_label(batch, us->nir->info.name);
   }
''', 'jm_launch_grid')
    open(P + 'pan_jm.c', 'w').write(u)

u = open(P + 'pan_cmdstream.c').read()
if 'B1L-0093' not in u:
    a = '''   if (shader_bo)
      perf_debug(batch->ctx, "Blend shader use");
}'''
    b = '''   if (shader_bo) {
      perf_debug(batch->ctx, "Blend shader use");
      b2l_batch_label(batch, "+blend-shader");   /* B2L (B1L-0093) */
   }
}'''
    if u.count(a) != 1:
        sys.exit('air-b2l: panfrost_get_blend_shaders is not as expected (B1L-0093)')
    u = u.replace(a, b)
    open(P + 'pan_cmdstream.c', 'w').write(u)
print('air-b2l: failed Mali chains said with their shaders')

# B1L-0094 (M6b): THE INSTRUCTION THE MALI STOPPED ON (B1L-0093 named A2Xghfc, the main fragment shader, in every
# failed chain; b2l-bicomp then hit "swz0 < 8" in bi_pack_add_swz_v4i8). bi_lower_swizzle replaces a swizzle an
# instruction cannot take with a SWZ before it; for an 8-bit instruction it always picks SWZ.v4i8, even when the
# swizzle is a halfword one (H00/H10/H11), which SWZ.v4i8 cannot encode: a release build packs garbage
# (INSTR_INVALID_ENC, 0x51). The same the other way: a byte swizzle on a 16-bit instruction went to SWZ.v2i16. The
# SWZ is now chosen by the swizzle's kind.
q = sys.argv[1] + '/../../panfrost/compiler/bi_lower_swizzle.c'
u = open(q).read()
if 'B1L-0094' not in u:
    a = """   bool is_8 = (bi_opcode_props[ins->op].size == BI_SIZE_8) ||
               (bi_opcode_props[ins->op].size == BI_SIZE_32 &&
                ins->src[src].swizzle >= BI_SWIZZLE_B0000);"""
    b = """   /* B2L (B1L-0094): which SWZ follows from the swizzle alone: a byte swizzle is SWZ.v4i8's (also on a 16-bit
    * instruction), a halfword swizzle SWZ.v2i16's (also on an 8-bit one); each cannot encode the other kind */
   bool is_8 = ins->src[src].swizzle >= BI_SWIZZLE_B0000;"""
    if u.count(a) != 1:
        sys.exit('air-b2l: bi_lower_swizzle is not as expected (B1L-0094)')
    u = u.replace(a, b)
    open(q, 'w').write(u)
print('air-b2l: halfword swizzles of 8-bit instructions through SWZ.v2i16')

# B1L-0094 (continued): IADD/ISUB.v4s8/v4u8 take no swizzle on their first source and only identity or a replicated
# byte on the second (bi_packer's lanes tables), but bi_lower_swizzle had no case for them, so a swizzle stayed and
# was packed as garbage. They are lowered like the shifts of v4i8.
q = sys.argv[1] + '/../../panfrost/compiler/bi_lower_swizzle.c'
u = open(q).read()
if 'B1L-0094b' not in u:
    a = """   case BI_OPCODE_LSHIFT_AND_V4I8:
   case BI_OPCODE_LSHIFT_OR_V4I8:"""
    b = """   case BI_OPCODE_IADD_V4S8:   /* B2L (B1L-0094b) */
   case BI_OPCODE_IADD_V4U8:
   case BI_OPCODE_ISUB_V4S8:
   case BI_OPCODE_ISUB_V4U8:
      if (src == 1 && (ins->src[src].swizzle == BI_SWIZZLE_H01 ||
                       bi_swizzle_replicates_8(ins->src[src].swizzle)))
         return;
      break;

""" + a
    if u.count(a) != 1:
        sys.exit('air-b2l: the v4i8 shifts in bi_lower_swizzle are not as expected (B1L-0094b)')
    u = u.replace(a, b)
    open(q, 'w').write(u)
print('air-b2l: IADD/ISUB v4i8 swizzles lowered')

# B1L-0095 (vitesse): KEEP THE MALI'S SHADERS ON THE PHONE. Every boot compiled backboardd's 212 pipelines again (the
# main fragment shader alone, A2Xghfc, is 33 s of Bifrost compile on the Mac): more than 10 minutes a boot. Mesa's
# disk cache (panfrost: pan_disk_cache.c, keyed by the NIR's SHA-1) is turned on (build-mesa.sh -Dshader-cache=enabled)
# and put in /b2l-dump/mesa-cache, the directory the kernel opens on USERDATA for the Metal work (B1L-0086). What
# names the driver build is the ELF build-id note, which a Mach-O image does not have (build_id_find_nhdr_for_addr is
# NULL here): it is the image's LC_UUID instead, which ld changes with the code -- so a fixed compiler never gets the
# binaries of an older one back.
q = sys.argv[1] + '/../../gallium/drivers/panfrost/pan_disk_cache.c'
u = open(q).read()
if 'B1L-0095' not in u:
    a = """   const struct build_id_note *note =
      build_id_find_nhdr_for_addr(panfrost_disk_cache_init);
   assert(note && build_id_length(note) == 20); /* sha1 */

   const uint8_t *id_sha1 = build_id_data(note);
   assert(id_sha1);

   char timestamp[41];
   _mesa_sha1_format(timestamp, id_sha1);
"""
    b = """   /* B2L (B1L-0095): the Mach-O image's LC_UUID names this build (no ELF build-id note here) */
   char timestamp[41] = "";
   {
      Dl_info info;
      if (dladdr((void *)panfrost_disk_cache_init, &info) && info.dli_fbase) {
         const struct mach_header_64 *mh = info.dli_fbase;
         const struct load_command *lc = (const void *)(mh + 1);
         for (uint32_t i = 0; i < mh->ncmds; i++) {
            if (lc->cmd == LC_UUID) {
               const uint8_t *id = ((const struct uuid_command *)lc)->uuid;
               for (unsigned k = 0; k < 16; k++)
                  snprintf(timestamp + 2 * k, 3, "%02x", id[k]);
               break;
            }
            lc = (const void *)((const char *)lc + lc->cmdsize);
         }
      }
   }
   if (!timestamp[0])
      return;   /* no identity, no cache: never a binary of another build */
"""
    if u.count(a) != 1:
        sys.exit('air-b2l: panfrost_disk_cache_init is not as expected (B1L-0095)')
    u = u.replace(a, b)
    u = u.replace('#include "util/build_id.h"\n', '#include "util/build_id.h"\n#include <dlfcn.h>\n#include <mach-o/loader.h>\n', 1)
    open(q, 'w').write(u)

q = sys.argv[1] + '/air_metal.c'
u = open(q).read()
a = """   setenv("NIR_DEBUG", "novalidate", 0);
}"""
if u.count(a) != 1:
    sys.exit('air-b2l: the NIR_DEBUG constructor is not as expected (B1L-0095)')
u = u.replace(a, """   setenv("NIR_DEBUG", "novalidate", 0);
   /* B2L (B1L-0095): Mesa's shader cache on USERDATA, so a boot after the first compiles nothing it has seen */
   setenv("MESA_SHADER_CACHE_DIR", "/b2l-dump/mesa-cache", 0);
   setenv("MESA_SHADER_CACHE_MAX_SIZE", "1G", 0);
}""")
open(q, 'w').write(u)
print('air-b2l: Mesa shader cache in /b2l-dump/mesa-cache, named by the image UUID')

# B1L-0099 (M6b, rendu): THE VIEWPORT RECTANGLE AND THE SCISSOR. The contract had only the viewport's depth range:
# every draw covered its whole attachment, and QuartzCore's blur chain (each downsample drawn into a quarter, then a
# sixteenth of a 768x1600 target: "asked for 384x800 at 0,0", 1126 times a boot) drew over the whole of it -- the
# glass of the Control Center broken, coloured triangles. The scissor was dropped by the driver as well. Both are
# appended to struct rwa_draw (the driver's copy is appended the same way) and given to Gallium: the viewport's
# scale/translate from its rectangle, and a scissor state (the rasterizer always scissors now, to the whole attachment
# when the encoder set none).
q = sys.argv[1] + '/air_metal.h'
u = open(q).read()
a = """	const struct rwa_buffer_bind *fragment_buffer_table;
	unsigned fragment_buffer_table_count;
};"""
if u.count(a) != 1:
    sys.exit('air-b2l: struct rwa_draw is not as expected (B1L-0099)')
u = u.replace(a, """	const struct rwa_buffer_bind *fragment_buffer_table;
	unsigned fragment_buffer_table_count;
	/* B2L (B1L-0099): appended */
	const float *b2l_viewport;     /* NULL, or { x, y, width, height }: MTLViewport's rectangle */
	const int *b2l_scissor;        /* NULL, or { x, y, width, height }: MTLScissorRect */
};""")
open(q, 'w').write(u)
q = sys.argv[1] + '/air_metal.c'
u = open(q).read()
def rep(a, b, what):
    global u
    if u.count(a) != 1:
        sys.exit('air-b2l: %s is not as expected (B1L-0099)' % what)
    u = u.replace(a, b)
rep("""         .depth_clip_near = 1, .depth_clip_far = 1, .clip_halfz = zs_format != 0,
         .multisample = samples > 1 });""", """         .depth_clip_near = 1, .depth_clip_far = 1, .clip_halfz = zs_format != 0,
         .multisample = samples > 1, .scissor = 1 /* B2L (B1L-0099) */ });""", 'the rasterizer state')
rep("""      .scale = { w / 2.0f, -(h / 2.0f), zfar - znear },
      .translate = { w / 2.0f, h / 2.0f, znear } });""", """      .scale = { b2l_vw / 2.0f, -(b2l_vh / 2.0f), zfar - znear },
      .translate = { b2l_vx + b2l_vw / 2.0f, b2l_vy + b2l_vh / 2.0f, znear } });
   {   /* B2L (B1L-0099): the scissor, clamped to the attachment; the whole of it when none was set */
      int sx0 = 0, sy0 = 0, sx1 = (int)w, sy1 = (int)h;
      if (draw->b2l_scissor) {
         sx0 = MAX2(draw->b2l_scissor[0], 0);
         sy0 = MAX2(draw->b2l_scissor[1], 0);
         sx1 = MIN2(draw->b2l_scissor[0] + draw->b2l_scissor[2], (int)w);
         sy1 = MIN2(draw->b2l_scissor[1] + draw->b2l_scissor[3], (int)h);
         if (sx1 < sx0) sx1 = sx0;
         if (sy1 < sy0) sy1 = sy0;
      }
      pipe->set_scissor_states(pipe, 0, 1, &(struct pipe_scissor_state){
         .minx = (unsigned)sx0, .miny = (unsigned)sy0, .maxx = (unsigned)sx1, .maxy = (unsigned)sy1 });
   }""", 'the viewport state')
rep("""   float znear = draw->depth_range ? draw->depth_range[0] : 0.0f;
   float zfar = draw->depth_range ? draw->depth_range[1] : 1.0f;""", """   float znear = draw->depth_range ? draw->depth_range[0] : 0.0f;
   float zfar = draw->depth_range ? draw->depth_range[1] : 1.0f;
   /* B2L (B1L-0099): the viewport's rectangle, the whole attachment when none was set */
   float b2l_vx = draw->b2l_viewport ? draw->b2l_viewport[0] : 0.0f;
   float b2l_vy = draw->b2l_viewport ? draw->b2l_viewport[1] : 0.0f;
   float b2l_vw = draw->b2l_viewport ? draw->b2l_viewport[2] : (float)w;
   float b2l_vh = draw->b2l_viewport ? draw->b2l_viewport[3] : (float)h;""", 'the depth range')
open(q, 'w').write(u)
print('air-b2l: viewport rectangle and scissor')

# B1L-0104 (rendu): TEXTURE VIEWS. -newTextureViewWithPixelFormat: answered nil ("the compiler library has no call that
# makes a second texture over the same memory"), and QuartzCore binds what it gets: compute_average_luma's texture 0
# was nil at every dispatch (158 a boot), the Liquid Glass tint following a luminance never measured. A view is a
# second rwa_texture over the same pipe_resource (one more reference) whose sampler view reads it in another format
# of the same block size -- what Gallium's sampler views are for.
q = sys.argv[1] + '/air_metal.c'
u = open(q).read()
def rep(a, b, what):
    global u
    if u.count(a) != 1:
        sys.exit('air-b2l: %s is not as expected (B1L-0104)' % what)
    u = u.replace(a, b)
rep("""   bool shader_read, shader_write;
};""", """   bool shader_read, shader_write;
   enum pipe_format b2l_view_format;   /* B2L (B1L-0104): a view's format, or PIPE_FORMAT_NONE */
};""", 'struct rwa_texture')
rep("""            u_sampler_view_default_template(&tmpl, t->res, t->res->format);""",
    """            u_sampler_view_default_template(&tmpl, t->res,
                                            t->b2l_view_format ? t->b2l_view_format : t->res->format);""",
    'the sampler view')
rep("""/*
 * A region of a texture, as Gallium wants it, and everything that will not fit in one
 * refused.""", """/* B2L (B1L-0104): a view of a texture in another pixel format of the same block size */
struct rwa_texture *rwa_texture_view(struct rwa_texture *base, unsigned mtl_format, char *why, size_t why_size);
struct rwa_texture *
rwa_texture_view(struct rwa_texture *base, unsigned mtl_format, char *why, size_t why_size)
{
   if (!base || !base->res) {
      say(why, why_size, "no texture to make a view of");
      return NULL;
   }
   const struct rwa_pixel_format *fmt = pixel_format(mtl_format);
   if (!fmt) {
      say(why, why_size, "pixel format %u is %s", mtl_format, pixel_format_refusal(mtl_format));
      return NULL;
   }
   if (util_format_get_blocksize(fmt->pipe) != util_format_get_blocksize(base->res->format) ||
       util_format_get_blockwidth(fmt->pipe) != util_format_get_blockwidth(base->res->format) ||
       util_format_get_blockheight(fmt->pipe) != util_format_get_blockheight(base->res->format)) {
      say(why, why_size, "a view of a %s texture as %s: the two formats have blocks of different sizes",
          base->format_name, fmt->name);
      return NULL;
   }
   struct rwa_texture *t = calloc(1, sizeof(*t));
   if (!t) {
      say(why, why_size, "out of memory making a texture view");
      return NULL;
   }
   *t = *base;
   t->view = NULL;
   t->res = NULL;
   pipe_resource_reference(&t->res, base->res);
   t->shape.pixel_format = mtl_format;
   t->format_name = fmt->name;
   t->b2l_view_format = fmt->pipe;
   return t;
}

/*
 * A region of a texture, as Gallium wants it, and everything that will not fit in one
 * refused.""", 'texture_box')
open(q, 'w').write(u)
print('air-b2l: texture views')

# B1L-0105 (vitesse): A FENCE PER COMMAND BUFFER. -[MTLCommandBuffer commit] with a completed handler (QuartzCore puts
# one on nearly every one) waited for ALL the GPU's work on the render thread (rwa_finish), so CoreAnimation could not
# encode the next frame while the GPU drew this one: 2-7 frames a second with the GPU 2-7 % busy. rwa_flush_fence
# hands back the fence of what was just submitted; rwa_fence_wait waits for it through the SCREEN (fence_finish with
# no context: safe from another thread, the context staying the render thread's alone); rwa_fence_release drops it.
q = sys.argv[1] + '/air_metal.c'
u = open(q).read()
a = """int
rwa_flush(struct rwa_device *device)
{"""
if u.count(a) != 1:
    sys.exit('air-b2l: rwa_flush is not as expected (B1L-0105)')
u = u.replace(a, """/* B2L (B1L-0105) */
void *rwa_flush_fence(struct rwa_device *device);
int rwa_fence_wait(struct rwa_device *device, void *fence, uint64_t timeout_ns);
void rwa_fence_release(struct rwa_device *device, void *fence);
void *
rwa_flush_fence(struct rwa_device *device)
{
   struct pipe_fence_handle *fence = NULL;
   if (!device || !device->pipe)
      return NULL;
   device->pipe->flush(device->pipe, &fence, 0);
   return fence;
}
int
rwa_fence_wait(struct rwa_device *device, void *fence, uint64_t timeout_ns)
{
   if (!device || !fence)
      return 0;
   return device->screen->fence_finish(device->screen, NULL, (struct pipe_fence_handle *)fence, timeout_ns) ? 0 : -1;
}
void
rwa_fence_release(struct rwa_device *device, void *fence)
{
   struct pipe_fence_handle *f = fence;
   if (device && f)
      device->screen->fence_reference(device->screen, &f, NULL);
}

""" + a)
open(q, 'w').write(u)
print('air-b2l: a fence per command buffer')

# B1L-0110 (vitesse): the diagnostics of B1L-0085..0087 off -- the shader modules sent through the kernel log
# (B2LMOD, base64, up to 400 KB a boot) and a line for every render pipeline: they served to find the invalid
# instructions, all fixed since B1L-0094.
q = sys.argv[1] + '/air_metal.c'
u = open(q).read()
for a, what in (("""   static int in_bbd = -1;
   static unsigned modules;""", 'b2l_dump_module'),
                ("""   static unsigned said;
   if (said >= 400)
      return;
   said++;
   unsigned n = 0;""", 'b2l_say_pipeline')):
    if u.count(a) != 1:
        sys.exit('air-b2l: %s is not as expected (B1L-0110)' % what)
    u = u.replace(a, "   if (1)\n      return;   /* B2L (B1L-0110): off */\n" + a)
open(q, 'w').write(u)
print('air-b2l: shader dumps and pipeline lines off')

# B1L-0115 (vitesse): UPLOAD ONLY WHAT A SHADER CAN READ OF A CONSTANT BUFFER. On panfrost every Metal constant buffer
# is a user_buffer (ubo_base 1: panfrost pushes UBO words from the processor), and panfrost_emit_const_buf copies its
# whole buffer_size into the batch AT EVERY DRAW. Without air.buffer_size (a `constant T *` argument) that size was
# all the rest of the MTLBuffer after the offset -- QuartzCore keeps its constants in large buffers. The sampler of
# B1L-0113 found backboardd's display thread at 35 % in that memcpy (panfrost_emit_const_buf+0x984) with the
# interface idle at 10 images/s. Now the loads of each constant buffer are bounded once, when the pipeline is made
# (a constant offset, or nir_unsigned_upper_bound of it), and only that much is copied; an unknown bound (an index or
# offset that cannot be bounded, get_ubo_size) keeps the old size. Said every 5 s: "B2L UBO: ...".
q = sys.argv[1] + '/nir_air.h'
u = open(q).read()
a = "   unsigned size;    /* air.buffer_size when given, else 0 */\n"
if u.count(a) != 1:
    sys.exit('air-b2l: struct air_resource is not as expected (B1L-0115)')
u = u.replace(a, a + "   unsigned b2l_read_end;   /* B2L (B1L-0115): bytes of this constant buffer its loads can reach; 0 unknown */\n")
open(q, 'w').write(u)

q = sys.argv[1] + '/air_metal.c'
u = open(q).read()
def rep115(a, b, what):
    global u
    if u.count(a) != 1:
        sys.exit('air-b2l: %s is not as expected (B1L-0115)' % what)
    u = u.replace(a, b)

rep115('#include <sys/sysctl.h>\n', '#include <sys/sysctl.h>\n#include <time.h>   /* B2L (B1L-0115): clock_gettime_nsec_np */\nstruct air_shader_info;\nstatic void b2l_ubo_read_ends(nir_shader *nir, struct air_shader_info *info);   /* B2L (B1L-0115) */\nstatic void b2l_ubo_stat(size_t full, size_t want, bool bounded);\n', 'includes')
rep115("""static void *
make_stage_cso(struct rwa_device *device, nir_shader *nir, char *why, size_t why_size)
{""", """/* B2L (B1L-0115): how far the loads of each constant buffer can reach, kept on its resource (b2l_read_end) */
static void
b2l_ubo_read_ends(nir_shader *nir, struct air_shader_info *info)
{
   uint32_t end[64] = { 0 };
   bool seen[64] = { false }, unbounded[64] = { false }, all_unknown = false;

   if (!nir || !info)
      return;
   struct hash_table *ht = _mesa_pointer_hash_table_create(NULL);
   nir_foreach_function_impl(impl, nir) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;
            nir_intrinsic_instr *in = nir_instr_as_intrinsic(instr);
            if (in->intrinsic != nir_intrinsic_load_ubo && in->intrinsic != nir_intrinsic_get_ubo_size)
               continue;
            if (!nir_src_is_const(in->src[0])) {
               all_unknown = true;
               continue;
            }
            uint64_t k = nir_src_as_uint(in->src[0]);
            if (k >= 64) {
               all_unknown = true;
               continue;
            }
            seen[k] = true;
            if (in->intrinsic == nir_intrinsic_get_ubo_size) {
               unbounded[k] = true;
               continue;
            }
            uint32_t bytes = in->def.num_components * MAX2(in->def.bit_size / 8, 1);
            /* (B1L-0124's "constant offsets only" was withdrawn in B1L-0125: the faults it was meant for came from
             * B1L-0123's level views, and grew with it) */
            uint64_t ub = nir_src_is_const(in->src[1])
                             ? nir_src_as_uint(in->src[1])
                             : nir_unsigned_upper_bound(nir, ht, nir_get_scalar(in->src[1].ssa, 0), NULL);
            if (ub >= (1u << 20)) {
               unbounded[k] = true;
               continue;
            }
            end[k] = MAX2(end[k], (uint32_t)ub + bytes);
         }
      }
   }
   _mesa_hash_table_destroy(ht, NULL);
   for (unsigned i = 0; i < info->num_resources; i++) {
      struct air_resource *r = &info->resources[i];
      r->b2l_read_end = 0;
      if (r->kind != AIR_RESOURCE_CONSTANT_BUFFER || all_unknown || r->index >= 64)
         continue;
      if (seen[r->index] && !unbounded[r->index])
         r->b2l_read_end = (end[r->index] + 15u) & ~15u;
   }
}

/* B2L (B1L-0115): what the constant buffers of backboardd's draws cost, said every 5 s */
static void
b2l_ubo_stat(size_t full, size_t want, bool bounded)
{
   static int in_bbd = -1;
   static uint64_t since, binds, sent, avoided, unknown;
   if (in_bbd < 0)
      in_bbd = getprogname() && !strcmp(getprogname(), "backboardd");
   if (!in_bbd)
      return;
   binds++;
   sent += want;
   avoided += full - want;
   if (!bounded)
      unknown++;
   uint64_t now = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
   if (!since)
      since = now;
   if (now - since >= 5000000000ull) {
      char line[200];
      int n = snprintf(line, sizeof(line), "B2L UBO: %llu s: %llu bindings, %llu KB copied, %llu KB avoided, %llu unbounded",
                       (unsigned long long)((now - since) / 1000000000ull), (unsigned long long)binds,
                       (unsigned long long)(sent >> 10), (unsigned long long)(avoided >> 10), (unsigned long long)unknown);
      if (n > 0)
         (void)sysctlbyname("kern.hakios.say", NULL, NULL, line, (size_t)MIN2(n, (int)sizeof(line) - 1));
      since = now;
      binds = sent = avoided = unknown = 0;
   }
}

static void *
make_stage_cso(struct rwa_device *device, nir_shader *nir, char *why, size_t why_size)
{""", 'make_stage_cso')

rep115("""   render->fs_cso = make_stage_cso(device, fs, why, why_size);
   if (!render->fs_cso)""", """   b2l_ubo_read_ends(fs, &render->finfo);   /* B2L (B1L-0115) */
   if (!render->generated_vertex)
      b2l_ubo_read_ends(vs, &render->vinfo);
   render->fs_cso = make_stage_cso(device, fs, why, why_size);
   if (!render->fs_cso)""", 'render stage creation')

rep115("""      say(why, why_size, "%s", error ? error : "the module could not be translated");
      goto fail;
   }
""", """      say(why, why_size, "%s", error ? error : "the module could not be translated");
      goto fail;
   }
   b2l_ubo_read_ends(compute->nir, &compute->info);   /* B2L (B1L-0115) */
""", 'compute creation')

rep115("""            size_t want = r->size && r->size <= left ? r->size : left;
""", """            size_t want = r->size && r->size <= left ? r->size : left;
            size_t b2l_full = want;   /* B2L (B1L-0115): only what the loads can reach */
            if (r->b2l_read_end && r->b2l_read_end < want)
               want = r->b2l_read_end;
            b2l_ubo_stat(b2l_full, want, r->b2l_read_end != 0);
""", 'constant buffer upload')
open(q, 'w').write(u)
print('air-b2l: constant buffers bounded to what their loads reach')


# B1L-0118 (shader states made once a shader) WITHDRAWN in B1L-0120: with it, B1L-0119 showed 544 GPU MMU faults at
# address 0 and 265 unfinished jobs (glass, blur and path passes) and the owner saw tearing and broken colours. The
# patch is kept in apply.py.with-0118 for a later, checked return.

# B1L-0122 (visuel): A BLIT ENCODER. -[MTLCommandBuffer blitCommandEncoder] answered nil, and Objective-C drops every
# message sent to nil without a word: each copy QuartzCore asked for (the backdrop under Liquid Glass, read back from
# what was already drawn) was never made, and the glass sampled a texture still at zero -- the black dock, the dark
# grey tiles of the Control Center. These are what the driver's new encoder records and runs at -commit, in order
# with the draws: texture copies through Gallium's blit (on the GPU), mipmaps (util_gen_mipmap), buffer copies and
# fills.
q = sys.argv[1] + '/air_metal.c'
u = open(q).read()
a = '#include "util/u_framebuffer.h"\n'
if u.count(a) != 1:
    sys.exit('air-b2l: includes are not as expected (B1L-0122)')
u = u.replace(a, a + '#include "util/box.h"              /* B2L (B1L-0122) */\n#include "util/u_gen_mipmap.h"\n#include "util/format/u_format.h"\n', 1)
u += r'''

/* B2L (B1L-0122): what -[MTLBlitCommandEncoder ...] asks, on the GPU, ordered with the draws by the context */
int rwa_texture_copy(struct rwa_texture *src, unsigned src_level, unsigned src_slice, const unsigned src_origin[3],
                     const unsigned size[3], struct rwa_texture *dst, unsigned dst_level, unsigned dst_slice,
                     const unsigned dst_origin[3], char *why, size_t why_size);
int rwa_texture_mipmaps(struct rwa_texture *t, char *why, size_t why_size);
int rwa_buffer_copy(struct rwa_buffer *src, size_t src_offset, struct rwa_buffer *dst, size_t dst_offset, size_t size,
                    char *why, size_t why_size);
int rwa_buffer_fill(struct rwa_buffer *b, size_t offset, size_t size, unsigned value, char *why, size_t why_size);
static enum pipe_format
b2l_blit_format(const struct rwa_texture *t)
{
   return t->b2l_view_format != PIPE_FORMAT_NONE ? t->b2l_view_format : t->res->format;
}

int
rwa_texture_copy(struct rwa_texture *src, unsigned src_level, unsigned src_slice, const unsigned src_origin[3],
                 const unsigned size[3], struct rwa_texture *dst, unsigned dst_level, unsigned dst_slice,
                 const unsigned dst_origin[3], char *why, size_t why_size)
{
   if (!src || !dst || !src->res || !dst->res || src->device != dst->device) {
      say(why, why_size, "a texture copy without two textures of one device");
      return -1;
   }
   if (src_level > src->res->last_level || dst_level > dst->res->last_level) {
      say(why, why_size, "a texture copy from level %u to level %u, past the textures' %u and %u", src_level,
          dst_level, src->res->last_level, dst->res->last_level);
      return -1;
   }
   if (src->res->nr_samples > 1 || dst->res->nr_samples > 1) {
      say(why, why_size, "a copy between multisample textures is not implemented");
      return -1;
   }
   struct pipe_context *pipe = dst->device->pipe;
   struct pipe_blit_info info;
   memset(&info, 0, sizeof(info));
   bool src3d = src->res->target == PIPE_TEXTURE_3D, dst3d = dst->res->target == PIPE_TEXTURE_3D;
   unsigned depth = MAX2(size[2], 1);
   info.src.resource = src->res;
   info.src.level = src_level;
   info.src.format = b2l_blit_format(src);
   u_box_3d(src_origin[0], src_origin[1], src3d ? src_origin[2] : src_slice, size[0], MAX2(size[1], 1),
            src3d ? depth : 1, &info.src.box);
   info.dst.resource = dst->res;
   info.dst.level = dst_level;
   info.dst.format = b2l_blit_format(dst);
   u_box_3d(dst_origin[0], dst_origin[1], dst3d ? dst_origin[2] : dst_slice, size[0], MAX2(size[1], 1),
            dst3d ? depth : 1, &info.dst.box);
   info.mask = util_format_get_mask(info.dst.format);
   info.filter = PIPE_TEX_FILTER_NEAREST;
   pipe->blit(pipe, &info);
   return 0;
}

int
rwa_texture_mipmaps(struct rwa_texture *t, char *why, size_t why_size)
{
   if (!t || !t->res) {
      say(why, why_size, "mipmaps for no texture");
      return -1;
   }
   if (t->res->last_level == 0)
      return 0;
   if (!util_gen_mipmap(t->device->pipe, t->res, b2l_blit_format(t), 0, t->res->last_level, 0,
                        util_max_layer(t->res, 0), PIPE_TEX_FILTER_LINEAR)) {
      say(why, why_size, "the driver would not make the mipmaps of a %s", t->format_name ? t->format_name : "texture");
      return -1;
   }
   return 0;
}

int
rwa_buffer_copy(struct rwa_buffer *src, size_t src_offset, struct rwa_buffer *dst, size_t dst_offset, size_t size,
                char *why, size_t why_size)
{
   if (!src || !dst || !src->res || !dst->res || src_offset > src->size || size > src->size - src_offset ||
       dst_offset > dst->size || size > dst->size - dst_offset) {
      say(why, why_size, "a buffer copy of %zu bytes outside its buffers", size);
      return -1;
   }
   if (!size)
      return 0;
   struct pipe_box box;
   u_box_1d((int)src_offset, (int)size, &box);
   dst->device->pipe->resource_copy_region(dst->device->pipe, dst->res, 0, (unsigned)dst_offset, 0, 0, src->res, 0,
                                           &box);
   return 0;
}

int
rwa_buffer_fill(struct rwa_buffer *b, size_t offset, size_t size, unsigned value, char *why, size_t why_size)
{
   if (!b || !b->res || offset > b->size || size > b->size - offset) {
      say(why, why_size, "a buffer fill of %zu bytes outside its buffer", size);
      return -1;
   }
   if (!size)
      return 0;
   uint8_t v = (uint8_t)value;
   b->device->pipe->clear_buffer(b->device->pipe, b->res, (unsigned)offset, (unsigned)size, &v, 1);
   return 0;
}
'''
open(q, 'w').write(u)
print('air-b2l: a blit encoder (texture copies, mipmaps, buffer copies and fills)')

# B1L-0123 (visuel): VIEWS OF SOME LEVELS OR SLICES, AND RENDERING INTO THEM. QuartzCore's blur and Liquid Glass work
# on mip chains: they draw into level 1, 2, ... of a texture through a view of one level (or a pass attachment's
# `level`), then sample them. -newTextureViewWithPixelFormat:textureType:levels:slices: answered nil for anything but
# the whole texture, and every render surface was made at level 0 with the size of level 0 -- so those passes drew
# nowhere and the glass sampled empty levels: black. A texture now carries the range of levels and layers it is a
# view of; sampler views, render surfaces (and with them the framebuffer size, from the view's shape), image views,
# blits and mipmaps use it.
q = sys.argv[1] + '/air_metal.c'
u = open(q).read()
def rep123(a, b, what, count=1):
    global u
    if u.count(a) != count:
        sys.exit('air-b2l: %s is not as expected (B1L-0123): %d' % (what, u.count(a)))
    u = u.replace(a, b)

rep123("""   enum pipe_format b2l_view_format;   /* B2L (B1L-0104): a view's format, or PIPE_FORMAT_NONE */
};""", """   enum pipe_format b2l_view_format;   /* B2L (B1L-0104): a view's format, or PIPE_FORMAT_NONE */
   /* B2L (B1L-0123): the levels and layers this is a view of; counts of 0 mean all of them, from 0 */
   unsigned b2l_base_level, b2l_level_count, b2l_base_layer, b2l_layer_count;
};""", 'struct rwa_texture')

rep123("""/* B2L (B1L-0104): a view of a texture in another pixel format of the same block size */
struct rwa_texture *rwa_texture_view(struct rwa_texture *base, unsigned mtl_format, char *why, size_t why_size);""",
"""/* B2L (B1L-0104): a view of a texture in another pixel format of the same block size */
struct rwa_texture *rwa_texture_view(struct rwa_texture *base, unsigned mtl_format, char *why, size_t why_size);
/* B2L (B1L-0123): ... and of some of its levels and layers */
struct rwa_texture *rwa_texture_view_range(struct rwa_texture *base, unsigned mtl_format, unsigned first_level,
                                           unsigned levels, unsigned first_layer, unsigned layers, char *why,
                                           size_t why_size);""", 'rwa_texture_view prototype')

# sampler views
rep123("""            u_sampler_view_default_template(&tmpl, t->res,
                                            t->b2l_view_format ? t->b2l_view_format : t->res->format);
""", """            u_sampler_view_default_template(&tmpl, t->res,
                                            t->b2l_view_format ? t->b2l_view_format : t->res->format);
            if (t->b2l_level_count) {   /* B2L (B1L-0123) */
               tmpl.u.tex.first_level = t->b2l_base_level;
               tmpl.u.tex.last_level = t->b2l_base_level + t->b2l_level_count - 1;
            }
            if (t->b2l_layer_count && t->res->target != PIPE_TEXTURE_3D) {
               tmpl.u.tex.first_layer = t->b2l_base_layer;
               tmpl.u.tex.last_layer = t->b2l_base_layer + t->b2l_layer_count - 1;
            }
            if (t->b2l_layer_count == 1 && t->shape.type == RWA_TYPE_2D)
               tmpl.target = PIPE_TEXTURE_2D;
""", 'sampler view template')

# image views
rep123("""            .u.tex.level = 0,
            .u.tex.first_layer = 0,
            .u.tex.last_layer = (t->shape.type == RWA_TYPE_3D ? t->res->depth0
                                                              : t->layers) - 1,""",
"""            .u.tex.level = t->b2l_base_level,   /* B2L (B1L-0123) */
            .u.tex.first_layer = t->b2l_base_layer,
            .u.tex.last_layer = t->b2l_base_layer + (t->shape.type == RWA_TYPE_3D ? t->res->depth0
                                                                                  : t->layers) - 1,""", 'image view')

# render surfaces
rep123("""&(struct pipe_surface){ .format = draw->color[i]->res->format })""",
"""&(struct pipe_surface){ .format = draw->color[i]->b2l_view_format ? draw->color[i]->b2l_view_format
                                                                   : draw->color[i]->res->format,
                                  .u.tex.level = draw->color[i]->b2l_base_level,   /* B2L (B1L-0123) */
                                  .u.tex.first_layer = draw->color[i]->b2l_base_layer,
                                  .u.tex.last_layer = draw->color[i]->b2l_base_layer })""", 'colour surfaces', 2)
rep123("""&(struct pipe_surface){ .format = draw->depth->res->format })""",
"""&(struct pipe_surface){ .format = draw->depth->res->format,
                                  .u.tex.level = draw->depth->b2l_base_level,   /* B2L (B1L-0123) */
                                  .u.tex.first_layer = draw->depth->b2l_base_layer,
                                  .u.tex.last_layer = draw->depth->b2l_base_layer })""", 'depth surfaces', 2)

# a clear-only pass: the framebuffer's size at the attachment's level
rep123("""   struct pipe_framebuffer_state fb = { .width = size_of->width0, .height = size_of->height0,""",
"""   unsigned b2l_lvl = 0;   /* B2L (B1L-0123): the level the attachments are views of */
   for (unsigned i = 0; i < draw->color_count; i++)
      if (draw->color[i]) {
         b2l_lvl = draw->color[i]->b2l_base_level;
         break;
      }
   if (!draw->color_count && draw->depth)
      b2l_lvl = draw->depth->b2l_base_level;
   struct pipe_framebuffer_state fb = { .width = u_minify(size_of->width0, b2l_lvl),
                                        .height = u_minify(size_of->height0, b2l_lvl),""", 'clear framebuffer')

# blits and mipmaps: levels and layers relative to the views
rep123("""   info.src.resource = src->res;
   info.src.level = src_level;""", """   src_level += src->b2l_base_level;   /* B2L (B1L-0123): relative to the views */
   dst_level += dst->b2l_base_level;
   src_slice += src->b2l_base_layer;
   dst_slice += dst->b2l_base_layer;
   info.src.resource = src->res;
   info.src.level = src_level;""", 'blit levels')
rep123("""   if (src_level > src->res->last_level || dst_level > dst->res->last_level) {""",
"""   if (src_level + src->b2l_base_level > src->res->last_level || dst_level + dst->b2l_base_level > dst->res->last_level) {""",
       'blit level check')
rep123("""   if (!util_gen_mipmap(t->device->pipe, t->res, b2l_blit_format(t), 0, t->res->last_level, 0,
                        util_max_layer(t->res, 0), PIPE_TEX_FILTER_LINEAR)) {""",
"""   unsigned b2l_first = t->b2l_base_level;   /* B2L (B1L-0123) */
   unsigned b2l_last = t->b2l_level_count ? t->b2l_base_level + t->b2l_level_count - 1 : t->res->last_level;
   if (b2l_last <= b2l_first)
      return 0;
   if (!util_gen_mipmap(t->device->pipe, t->res, b2l_blit_format(t), b2l_first, b2l_last, 0,
                        util_max_layer(t->res, 0), PIPE_TEX_FILTER_LINEAR)) {""", 'mipmaps range')

u += r'''

/* B2L (B1L-0123): a view of some levels and layers, in the same or another format of the same block size */
struct rwa_texture *
rwa_texture_view_range(struct rwa_texture *base, unsigned mtl_format, unsigned first_level, unsigned levels,
                       unsigned first_layer, unsigned layers, char *why, size_t why_size)
{
   if (!base || !base->res) {
      say(why, why_size, "no texture to make a view of");
      return NULL;
   }
   struct pipe_resource *res = base->res;
   unsigned abs_level = base->b2l_base_level + first_level;
   unsigned abs_layer = base->b2l_base_layer + first_layer;
   unsigned all_layers = res->target == PIPE_TEXTURE_3D ? 1 : MAX2(res->array_size, 1);
   if (!levels || abs_level + levels - 1 > res->last_level) {
      say(why, why_size, "a view of levels %u..%u of a texture with %u", abs_level, abs_level + levels - 1,
          res->last_level + 1);
      return NULL;
   }
   if (!layers || (res->target != PIPE_TEXTURE_3D && abs_layer + layers > all_layers)) {
      say(why, why_size, "a view of layers %u..%u of a texture with %u", abs_layer, abs_layer + layers - 1,
          all_layers);
      return NULL;
   }
   struct rwa_texture *t;
   if (mtl_format && mtl_format != base->shape.pixel_format) {
      t = rwa_texture_view(base, mtl_format, why, why_size);
      if (!t)
         return NULL;
   } else {
      t = calloc(1, sizeof(*t));
      if (!t) {
         say(why, why_size, "out of memory making a texture view");
         return NULL;
      }
      *t = *base;
      t->view = NULL;
      t->res = NULL;
      pipe_resource_reference(&t->res, base->res);
   }
   t->b2l_base_level = abs_level;
   t->b2l_level_count = levels;
   t->b2l_base_layer = res->target == PIPE_TEXTURE_3D ? 0 : abs_layer;
   t->b2l_layer_count = res->target == PIPE_TEXTURE_3D ? 0 : layers;
   t->shape.width = u_minify(res->width0, abs_level);
   if (t->shape.height)
      t->shape.height = u_minify(res->height0, abs_level);
   if (res->target == PIPE_TEXTURE_3D)
      t->shape.depth = u_minify(res->depth0, abs_level);
   t->shape.mip_levels = levels;
   if (res->target != PIPE_TEXTURE_3D) {
      t->layers = layers;
      if (t->shape.array_length > 1)
         t->shape.array_length = layers;
   }
   /* one slice of an array or one face of a cube is a 2D texture: what a render pass attachment must be */
   if (layers == 1 && (t->shape.type == RWA_TYPE_2D_ARRAY || t->shape.type == RWA_TYPE_CUBE ||
                       t->shape.type == RWA_TYPE_CUBE_ARRAY)) {
      t->shape.type = RWA_TYPE_2D;
      t->shape.array_length = 1;
   }
   return t;
}
'''
open(q, 'w').write(u)
print('air-b2l: views of some levels and layers, rendered into')

# B1L-0128 (visuel): ATTACHMENTS OF DIFFERENT SIZES. Metal allows them -- the render area is the smallest -- and
# QuartzCore's mip-chain passes use them (B1L-0127: 170 draws refused, attachment 1 always twice attachment 0). The
# framebuffer is now the smallest of the attachments instead of a refusal.
q = sys.argv[1] + '/air_metal.c'
u = open(q).read()
for a, b, what in (("""      } else if (tw != w || th != h) {
         say(why, why_size, "colour attachment %u is %ux%u and attachment 0 is %ux%u", i,
             tw, th, w, h);
         return -1;
      }""", """      } else if (tw != w || th != h) {   /* B2L (B1L-0128): Metal's render area, the smallest */
         w = MIN2(w, tw);
         h = MIN2(h, th);
      }""", 'colour sizes'),
                   ("""      } else if (tw != w || th != h) {
         say(why, why_size, "the depth attachment is %ux%u and colour attachment 0 is %ux%u",
             tw, th, w, h);
         return -1;
      }""", """      } else if (tw != w || th != h) {   /* B2L (B1L-0128) */
         w = MIN2(w, tw);
         h = MIN2(h, th);
      }""", 'depth size')):
    if u.count(a) != 1:
        sys.exit('air-b2l: %s is not as expected (B1L-0128)' % what)
    u = u.replace(a, b)
open(q, 'w').write(u)
print('air-b2l: attachments of different sizes drawn in the smallest')

# B1L-0129: Metal's load action is per colour attachment, and so is its clear colour. The driver passed attachment 0's
# alone, which Gallium applied to every attachment: QuartzCore's path passes (SF Symbols glyphs, clock hands, shapes)
# keep the picture in attachment 0 (Load) and clear their coverage attachment (Clear), so the coverage was never
# cleared; a pass clearing attachment 0 also wiped the others. struct rwa_draw gains b2l_clear_colors and
# b2l_clear_mask (appended, the driver's copy the same way); each attachment is cleared alone, with its own colour.
q = sys.argv[1] + '/air_metal.h'
u = open(q).read()
a = """	const int *b2l_scissor;        /* NULL, or { x, y, width, height }: MTLScissorRect */
};"""
if u.count(a) != 1:
    sys.exit('air-b2l: struct rwa_draw is not as expected (B1L-0129)')
u = u.replace(a, """	const int *b2l_scissor;        /* NULL, or { x, y, width, height }: MTLScissorRect */
	/* B2L (B1L-0129): appended */
	const float *b2l_clear_colors; /* NULL, or 8 x 4 floats: each colour attachment's clear colour */
	unsigned b2l_clear_mask;       /* the colour attachments whose load action is Clear */
};""")
open(q, 'w').write(u)
q = sys.argv[1] + '/air_metal.c'
u = open(q).read()
for a, b, what in (
        ("""/* B2L (B1L-0070): a pass with no draw in it, whose only work is its load actions. */
int
rwa_render_clear(""", """/* B2L (B1L-0129): each colour attachment cleared alone, with its own colour */
static void
b2l_clear_colors(struct pipe_context *pipe, const struct rwa_draw *draw)
{
   for (unsigned i = 0; i < draw->color_count && i < 8; i++) {
      if (!((draw->b2l_clear_mask >> i) & 1))
         continue;
      union pipe_color_union c;
      for (unsigned k = 0; k < 4; k++)
         c.f[k] = draw->b2l_clear_colors[4 * i + k];
      pipe->clear(pipe, PIPE_CLEAR_COLOR0 << i, NULL, &c, 0.0, 0);
   }
}

/* B2L (B1L-0070): a pass with no draw in it, whose only work is its load actions. */
int
rwa_render_clear(""", 'rwa_render_clear'),
        ("""      if (draw->clear_color)
         mask |= PIPE_CLEAR_COLOR0 << i;""", """      if (draw->b2l_clear_colors ? (draw->b2l_clear_mask >> i) & 1 : draw->clear_color != NULL)   /* B1L-0129 */
         mask |= PIPE_CLEAR_COLOR0 << i;""", 'the clear-only mask'),
        ("""   b2l_fb_set(device, &fb);
   {
      union pipe_color_union clear;
      for (unsigned c = 0; c < 4; c++)
         clear.f[c] = draw->clear_color ? draw->clear_color[c] : 0.0f;
      pipe->clear(pipe, mask, NULL, &clear,""", """   b2l_fb_set(device, &fb);
   if (draw->b2l_clear_colors) {   /* B2L (B1L-0129) */
      b2l_clear_colors(pipe, draw);
      mask &= ~PIPE_CLEAR_COLOR;
   }
   if (mask) {
      union pipe_color_union clear;
      for (unsigned c = 0; c < 4; c++)
         clear.f[c] = draw->clear_color ? draw->clear_color[c] : 0.0f;
      pipe->clear(pipe, mask, NULL, &clear,""", 'the clear-only clear'),
        ("""      if (draw->clear_color)
         mask |= PIPE_CLEAR_COLOR;""", """      if (draw->b2l_clear_colors)   /* B2L (B1L-0129): each attachment its own */
         b2l_clear_colors(pipe, draw);
      else if (draw->clear_color)
         mask |= PIPE_CLEAR_COLOR;""", 'the draw clear')):
    if u.count(a) != 1:
        sys.exit('air-b2l: %s is not as expected (B1L-0129)' % what)
    u = u.replace(a, b)
open(q, 'w').write(u)
print('air-b2l: each colour attachment its own load action and clear colour')

# B1L-0131: a colour target the fragment shader READS (framebuffer fetch: Metal's [[color(n)]] argument) is preloaded
# into the tile buffer even when the draw does not write it. Gallium panfrost sets batch->read only for depth and
# stencil, and preloads a colour target only if a draw writes it (blend info enabled) -- so a target that is only read
# came back as whatever the tile held. QuartzCore iOS 26 draws every layer into BGRA8 + an RGBA16Float second
# attachment, and its vibrant glyphs (Control Center, widgets) read them back (fixed_frag reads colour 0, 1 and 2).
q = sys.argv[1] + '/../../gallium/drivers/panfrost/pan_cmdstream.c'
u = open(q).read()
if 'B1L-0131' not in u:
    a = """   if (dirty & PAN_DIRTY_ZS)
      panfrost_set_batch_masks_zs(batch);
"""
    if u.count(a) != 1:
        sys.exit('air-b2l: panfrost_update_state_3d is not as expected (B1L-0131)')
    u = u.replace(a, a + """
   /* B2L (B1L-0131): the targets the fragment shader reads are preloaded, written or not */
   if (ctx->prog[PIPE_SHADER_FRAGMENT])
      batch->read |= ctx->prog[PIPE_SHADER_FRAGMENT]->info.fs.outputs_read * PIPE_CLEAR_COLOR0;
""")
    open(q, 'w').write(u)
print('air-b2l: colour targets read by framebuffer fetch preloaded')

# B1L-0185: a resource's buffer taken back from panfrost's BO cache is zeroed, as a fresh one is (the kernel makes every
# buffer zero). On Apple's GPUs a new texture starts at zero and QuartzCore draws as if it does; here a recycled buffer
# kept the pixels of whatever used it before -- first frames shown with a foreign tint, rounded corners in a stale colour.
q = sys.argv[1] + '/../../gallium/drivers/panfrost/pan_bo.c'
u = open(q).read()
if 'B1L-0185' not in u:
    a = """   bo = panfrost_bo_cache_fetch(dev, size, flags, label, true);
   if (!bo)
      bo = panfrost_bo_alloc(dev, size, flags, label);
   if (!bo)
      bo = panfrost_bo_cache_fetch(dev, size, flags, label, false);
   if (!bo) {
      panfrost_bo_cache_evict_all(dev);
      bo = panfrost_bo_alloc(dev, size, flags, label);
   }
"""
    b = """   p_atomic_set(&bo->refcnt, 1);

   if (dev->debug & (PAN_DBG_TRACE | PAN_DBG_SYNC)) {
"""
    if u.count(a) != 1 or u.count(b) != 1 or u.count('#include <stdio.h>\n') != 1:
        sys.exit('air-b2l: panfrost_bo_create is not as expected (B1L-0185)')
    u = u.replace('#include <stdio.h>\n', '#include <stdio.h>\n#include <string.h>\n')
    u = u.replace(a, """   bool b2l_reused = true;   /* B2L (B1L-0185): did this buffer come back from the cache? */
   bo = panfrost_bo_cache_fetch(dev, size, flags, label, true);
   if (!bo) {
      bo = panfrost_bo_alloc(dev, size, flags, label);
      b2l_reused = bo == NULL;
   }
   if (!bo)
      bo = panfrost_bo_cache_fetch(dev, size, flags, label, false);
   if (!bo) {
      panfrost_bo_cache_evict_all(dev);
      bo = panfrost_bo_alloc(dev, size, flags, label);
      b2l_reused = false;
   }
""")
    u = u.replace(b, """   p_atomic_set(&bo->refcnt, 1);

   /* B2L (B1L-0185): a resource's recycled buffer starts at zero, as a fresh one does */
   if (b2l_reused && (flags & PAN_BO_DELAY_MMAP) && !(flags & PAN_BO_INVISIBLE) &&
       panfrost_bo_mmap(bo) == 0)
      memset(bo->ptr.cpu, 0, panfrost_bo_size(bo));

   if (dev->debug & (PAN_DBG_TRACE | PAN_DBG_SYNC)) {
""")
    open(q, 'w').write(u)
print('air-b2l: recycled resource buffers zeroed')

# B1L-0299 (C1): small buffers stay out of the kernel's purgeable list while in panfrost's BO cache. backboardd made
# 16 000-23 000 madvise calls (mali 72) every 10 s in 0297, most of them for the 64 KB batch pools that go in and out of
# the cache every frame. A small buffer kept in the cache is freed within 2 s anyway (evict_stale_bos); only buffers
# of 256 KB and more still go through make_evictable / make_unevictable, so the big ones can still be taken back.
q = sys.argv[1] + '/../../gallium/drivers/panfrost/pan_bo.c'
u = open(q).read()
if 'B1L-0299' not in u:
    a = """      if (!pan_kmod_bo_make_unevictable(entry->kmod_bo)) {"""
    b = """   pan_kmod_bo_make_evictable(bo->kmod_bo);
"""
    if u.count(a) != 1 or u.count(b) != 1:
        sys.exit('air-b2l: panfrost BO cache is not as expected (B1L-0299)')
    u = u.replace(a, """      /* B2L (B1L-0299): a small buffer was never made evictable, see panfrost_bo_cache_put */
      if (panfrost_bo_size(entry) >= B2L_EVICTABLE_MIN && !pan_kmod_bo_make_unevictable(entry->kmod_bo)) {""")
    u = u.replace(b, """   /* B2L (B1L-0299): small buffers skip the madvise round trip; evict_stale_bos frees them within 2 s */
   if (panfrost_bo_size(bo) >= B2L_EVICTABLE_MIN)
      pan_kmod_bo_make_evictable(bo->kmod_bo);
""")
    u = u.replace('#include <string.h>\n', '#include <string.h>\n#define B2L_EVICTABLE_MIN (256 * 1024)   /* B2L (B1L-0299) */\n', 1)
    if 'define B2L_EVICTABLE_MIN' not in u:
        sys.exit('air-b2l: pan_bo.c has no <string.h> to follow (B1L-0299)')
    open(q, 'w').write(u)
print('air-b2l: small cached buffers skip madvise')

# B1L-0305: panfrost's BO cache holds at most 160 MB; past that, the oldest buffers are freed. Since B1L-0302 the kernel
# no longer takes back purgeable buffers while memory is still available, and backboardd's cache grew to 623 MB
# (1126 MB of GPU memory in all, 0303) -- smooth at first, then slow. Mesa's own rule (older than 2 s) stays.
q = sys.argv[1] + '/../../gallium/drivers/panfrost/pan_bo.c'
u = open(q).read()
if 'B1L-0305' not in u:
    def r305(a, b, what):
        global u
        if u.count(a) != 1:
            sys.exit('air-b2l: pan_bo.c %s is not as expected (B1L-0305)' % what)
        u = u.replace(a, b)
    r305('#define B2L_EVICTABLE_MIN (256 * 1024)   /* B2L (B1L-0299) */\n',
         '#define B2L_EVICTABLE_MIN (256 * 1024)   /* B2L (B1L-0299) */\n'
         '#define B2L_CACHE_MAX (160ull << 20)   /* B2L (B1L-0305) */\n'
         'static unsigned long long b2l_cache_bytes;   /* B2L (B1L-0305): what the BO cache holds, under bo_cache.lock; shortcut: one count per process, right while a process has one panfrost device -- per device if one ever has two */\n', 'defines')
    r305("""      /* This one works, splice it out of the cache */
      list_del(&entry->bucket_link);
      list_del(&entry->lru_link);
""", """      /* This one works, splice it out of the cache */
      list_del(&entry->bucket_link);
      list_del(&entry->lru_link);
      b2l_cache_bytes -= panfrost_bo_size(entry);   /* B2L (B1L-0305) */
""", 'fetch')
    r305("""         break;

      list_del(&entry->bucket_link);
      list_del(&entry->lru_link);
      panfrost_bo_free(entry);""", """         break;

      list_del(&entry->bucket_link);
      list_del(&entry->lru_link);
      b2l_cache_bytes -= panfrost_bo_size(entry);   /* B2L (B1L-0305) */
      panfrost_bo_free(entry);""", 'evict_stale')
    r305("""         list_del(&entry->lru_link);
         panfrost_bo_free(entry);""", """         list_del(&entry->lru_link);
         b2l_cache_bytes -= panfrost_bo_size(entry);   /* B2L (B1L-0305) */
         panfrost_bo_free(entry);""", 'evict_all')
    r305("""   list_addtail(&bo->lru_link, &dev->bo_cache.lru);
   clock_gettime(CLOCK_MONOTONIC, &time);""", """   list_addtail(&bo->lru_link, &dev->bo_cache.lru);
   b2l_cache_bytes += panfrost_bo_size(bo);   /* B2L (B1L-0305) */
   clock_gettime(CLOCK_MONOTONIC, &time);""", 'put')
    r305("""   bo->label = "Unused (BO cache)";

   /* Must be last */
   pthread_mutex_unlock(&dev->bo_cache.lock);""", """   bo->label = "Unused (BO cache)";

   /* B2L (B1L-0305): over the cap, the oldest go (this one too, last, if it alone is over: it is freed here) */
   while (b2l_cache_bytes > B2L_CACHE_MAX && !list_is_empty(&dev->bo_cache.lru)) {
      struct panfrost_bo *old = list_first_entry(&dev->bo_cache.lru, struct panfrost_bo, lru_link);
      list_del(&old->bucket_link);
      list_del(&old->lru_link);
      b2l_cache_bytes -= panfrost_bo_size(old);
      panfrost_bo_free(old);
   }

   /* Must be last */
   pthread_mutex_unlock(&dev->bo_cache.lock);""", 'put tail')
    open(q, 'w').write(u)
print('air-b2l: BO cache capped at 160 MB')

# B1L-0306: a buffer keeps its processor mapping while it sits in panfrost's BO cache; it is unmapped only when it is
# really freed (panfrost_bo_free). Before, every trip through the cache unmapped it (IOConnectUnmapMemory64: pmap_remove
# of wired pages, the first kernel cost of backboardd after the thread switches in 0304's call chains) and mapped it again
# on reuse (IOConnectMapMemory64 with kIOMapPrefault since B1L-0293: every page entered again).
q = sys.argv[1] + '/../../gallium/drivers/panfrost/pan_bo.c'
u = open(q).read()
if 'B1L-0306' not in u:
    def r306(a, b, what):
        global u
        if u.count(a) != 1:
            sys.exit('air-b2l: pan_bo.c %s is not as expected (B1L-0306)' % what)
        u = u.replace(a, b)
    r306("""static void
panfrost_bo_free(struct panfrost_bo *bo)
{
""", """static void panfrost_bo_munmap(struct panfrost_bo *bo);   /* B2L (B1L-0306) */

static void
panfrost_bo_free(struct panfrost_bo *bo)
{
   panfrost_bo_munmap(bo);   /* B2L (B1L-0306): the mapping lived on in the BO cache */
""", 'free')
    r306("""      /* When the reference count goes to zero, we need to cleanup */
      panfrost_bo_munmap(bo);
""", """      /* When the reference count goes to zero, we need to cleanup */
      /* B2L (B1L-0306): not unmapped here; panfrost_bo_free does it if the cache does not keep it */
""", 'unreference')
    open(q, 'w').write(u)
print('air-b2l: cached buffers keep their mapping')


# B1L-0224 (diagnostic): the modules of the render pipelines RenderBox's processes (SpringBoard, iconservicesagent,
# chronod) are refused, through the kernel's log (B2LMOD lines, scripts/extract-b2lmod.py), so the Mac's b2l-bicomp can
# run them through AIR -> NIR -> Bifrost; and one line a refused pipeline naming both modules by entry and checksum.
# Only refused pipelines are sent (their specialisations are what B1L-0188..0190 unlocked and what crashed Bifrost),
# 40 modules and 1 MB of bitcode a process at most. backboardd sends nothing: it has no refusal.
q = sys.argv[1] + '/air_metal.c'
u = open(q).read()
def rep224(a, b, what):
    global u
    if u.count(a) != 1:
        sys.exit('air-b2l: %s is not as expected (B1L-0224)' % what)
    u = u.replace(a, b)
rep224("""   if (1)
      return;   /* B2L (B1L-0110): off */
   static int in_bbd = -1;
   static unsigned modules;""", """   static int in_bbd = -1;   /* B2L (B1L-0224): now "a RenderBox process" */
   static size_t b2l_sent;
   static unsigned modules;""", 'b2l_dump_module (off)')
rep224("""   if (in_bbd < 0)
      in_bbd = getprogname() && !strcmp(getprogname(), "backboardd");
   if (!in_bbd || !bitcode || !size || modules >= 40 || size > 400000)
      return;""", """   if (in_bbd < 0)
      in_bbd = getprogname() && (!strcmp(getprogname(), "SpringBoard") ||
                                 !strcmp(getprogname(), "iconservicesagent") || !strcmp(getprogname(), "chronod"));
   if (!in_bbd || !bitcode || !size || modules >= 40 || size > 400000 || b2l_sent + size > 1000000)
      return;""", 'b2l_dump_module (who)')
rep224("""   snprintf(seen[modules++], sizeof(seen[0]), "%s", name);""",
       """   snprintf(seen[modules++], sizeof(seen[0]), "%s", name);
   b2l_sent += size;""", 'b2l_dump_module (seen)')
rep224("""   b2l_dump_module("f", render->finfo.entry_point, fragment_bitcode, fragment_size);   /* B2L (B1L-0086) */
   b2l_dump_module("v", vertex_entry ? vertex_entry : render->finfo.entry_point, vertex_bitcode, vertex_size);
""", "", 'the B1L-0086 dump calls')
rep224("""fail:
   rwa_render_destroy(render);
   return NULL;
}""", """fail:
   {   /* B2L (B1L-0224): a refused pipeline, named, and its modules sent */
      static unsigned b2l_refused;
      uint32_t hf = 2166136261u, hv = 2166136261u;
      for (size_t k = 0; fragment_bitcode && k < fragment_size; k++)
         hf = (hf ^ ((const unsigned char *)fragment_bitcode)[k]) * 16777619u;
      for (size_t k = 0; vertex_bitcode && k < vertex_size; k++)
         hv = (hv ^ ((const unsigned char *)vertex_bitcode)[k]) * 16777619u;
      char fe[120], ve[120];
      snprintf(fe, sizeof(fe), "%.80s-%08x", fragment_entry && fragment_entry[0] ? fragment_entry : "unnamed", hf);
      snprintf(ve, sizeof(ve), "%.80s-%08x", vertex_entry && vertex_entry[0] ? vertex_entry : "unnamed", hv);
      if (b2l_refused < 200) {
         char line[400];
         int n = snprintf(line, sizeof(line), "B2L REFUSED %s: f-%s (%zu bytes) v-%s (%zu bytes): %s",
                          getprogname() ? getprogname() : "?", fe, fragment_size, ve, vertex_size,
                          why && why[0] ? why : "?");
         if (n > 0) {
            b2l_refused++;
            (void)sysctlbyname("kern.hakios.say", NULL, NULL, line, (size_t)MIN2(n, (int)sizeof(line) - 1));
         }
      }
      b2l_dump_module("f", fe, fragment_bitcode, fragment_size);
      b2l_dump_module("v", ve, vertex_bitcode, vertex_size);
   }
   rwa_render_destroy(render);
   return NULL;
}""", 'rwa_render_create fail')
open(q, 'w').write(u)
print('air-b2l: refused RenderBox pipelines named and their modules sent (B1L-0224)')

# B1L-0226: phis of POINTERS into one buffer (B1L-0188's pointer half, with B1L-0190's select refinement), and nothing
# else: the two path pipelines SpringBoard asks for (vkba6pa7dhmha4 / vjba2ca3la7dhmha4 vertex, a binary search over
# path segments) and uber_vertex translate; checked on the Mac with the modules of B1L-0225 (out/b2lmod-0225, b2l-bicomp,
# Mesa with assertions): 4960 and 4432 bytes of Bifrost code. The struct half and the early-return order are B1L-0227.
# Only the byte offset of a pointer that walks ONE buffer goes in a register; QuartzCore's NIR is unchanged (compared).
t = sys.argv[1] + '/air_to_nir.c'
u = open(t).read()
def rep226(a, b, what):
    global u
    if u.count(a) != 1:
        sys.exit('air-b2l: %s is not as expected (B1L-0226)' % what)
    u = u.replace(a, b)

rep226("""static bool
resolve_pointer_phi(struct air_ctx *c, const struct air_inst *in, const bool *reachable)
{
   struct air_val *first = NULL;
""", """/* B2L (B1L-0188): two pointers into one piece of byte-addressed memory, which differ by their offset alone */
static bool
b2l_ptr_same_memory(const struct air_ptr *a, const struct air_ptr *b)
{
   return a->class == b->class && a->binding == b->binding && a->is_image == b->is_image &&
          (a->class == AIR_PTR_UBO || a->class == AIR_PTR_SSBO || a->class == AIR_PTR_SHARED ||
           a->class == AIR_PTR_SCRATCH || a->class == AIR_PTR_CONSTANT_DATA);
}

/* B2L (B1L-0190): the first resolved pointer under a choice of pointers */
static const struct air_val *
b2l_ptr_leaf(const struct air_val *v)
{
   while (v && v->kind == AIR_VAL_PTR_SELECT && v->num_elems == 2)
      v = v->elems[0];
   return v && v->kind == AIR_VAL_PTR ? v : NULL;
}

/* B2L (B1L-0190): the byte offset of a pointer into `like`'s memory; a choice between two is a bcsel of theirs */
static nir_def *
b2l_ptr_offset(struct air_ctx *c, const struct air_val *v, const struct air_ptr *like)
{
   if (v && v->kind == AIR_VAL_PTR_SELECT && v->num_elems == 2) {
      nir_def *x = b2l_ptr_offset(c, v->elems[0], like), *y = b2l_ptr_offset(c, v->elems[1], like);
      return x && y ? nir_bcsel(&c->b, v->def, x, y) : NULL;
   }
   if (!v || v->kind != AIR_VAL_PTR || !b2l_ptr_same_memory(&v->ptr, like))
      return NULL;
   return v->ptr.offset ? v->ptr.offset : nir_imm_int(&c->b, 0);
}

static bool
resolve_pointer_phi(struct air_ctx *c, const struct air_inst *in, const bool *reachable)
{
   bool b2l_reg = false;   /* B2L (B1L-0188): the offset differs between edges, or comes over a loop edge */
   struct air_val *first = NULL;
""", 'resolve_pointer_phi')

rep226("""      if (!e)
         return air_error(c, "instruction: a phi of pointers whose operand comes in over "
                             "a loop edge, so which resource it is depends on the "
                             "iteration");
      struct air_val *v = e->data;""", """      if (!e) {
         b2l_reg = true;   /* B2L (B1L-0188): checked against the resource when the stores are made */
         continue;
      }
      struct air_val *v = e->data;
      if (v && v->kind == AIR_VAL_PTR_SELECT && b2l_ptr_leaf(v) &&
          b2l_ptr_same_memory(&b2l_ptr_leaf(v)->ptr, &b2l_ptr_leaf(v)->ptr)) {   /* B2L (B1L-0190) */
         b2l_reg = true;
         if (!first)
            first = (struct air_val *)b2l_ptr_leaf(v);
         continue;
      }""", 'the loop-edge refusal')

rep226("""      else if (!same_ptr(&first->ptr, &v->ptr))
         return air_error(c, "instruction: a phi between pointers that name different "
                             "places, which would have to be chosen at run time");
   }
   if (!first)
      first = null_tex;""", """      else if (!same_ptr(&first->ptr, &v->ptr)) {
         if (!b2l_ptr_same_memory(&first->ptr, &v->ptr))
            return air_error(c, "instruction: a phi between pointers that name different "
                                "places, which would have to be chosen at run time");
         b2l_reg = true;
      }
   }
   if (b2l_reg) {   /* B2L (B1L-0188): one resource, the byte offset in a register */
      if (!first || !b2l_ptr_same_memory(&first->ptr, &first->ptr))
         return air_error(c, "instruction: a phi of pointers whose operand comes in over "
                             "a loop edge, so which resource it is depends on the "
                             "iteration");
      nir_builder entry = nir_builder_at(nir_before_impl(c->b.impl));
      nir_def *reg = nir_decl_reg(&entry, 1, 32, 0);
      _mesa_hash_table_insert(c->phi_regs, in, reg);
      struct air_ptr p = first->ptr;
      p.offset = nir_load_reg(&c->b, reg);
      air_set(c, &in->base, air_val_ptr(c, p));
      return true;
   }
   if (!first)
      first = null_tex;""", 'the different-places refusal')



rep226("""            c->b.cursor = *(nir_cursor *)ce->data;
            nir_def *val = air_get_ssa(c, in->operands[k]);
            if (!val)
               return false;
            nir_store_reg(&c->b, val, re->data);""", """            c->b.cursor = *(nir_cursor *)ce->data;
            if (in->base.type->kind == AIR_TYPE_POINTER) {   /* B2L (B1L-0188): the offset of the same resource */
               struct air_val *pv = air_get(c, in->operands[k]);
               struct hash_entry *me = _mesa_hash_table_search(c->values, &in->base);
               if (!pv)
                  return false;
               nir_def *po = me ? b2l_ptr_offset(c, pv, &((struct air_val *)me->data)->ptr) : NULL;
               if (!po)
                  return air_error(c, "instruction: a phi of pointers whose operand comes in over "
                                      "a loop edge and names another resource");
               nir_store_reg(&c->b, po, re->data);
               *(nir_cursor *)ce->data = c->b.cursor;
               continue;
            }
            nir_def *val = air_get_ssa(c, in->operands[k]);
            if (!val)
               return false;
            nir_store_reg(&c->b, val, re->data);""", 'the phi store')

open(t, 'w').write(u)
print('air-b2l: phis of pointers into one buffer translated (B1L-0226)')

# B1L-0227: phis of STRUCTS (B1L-0188's struct half: one register a leaf), for fa16dhmha4, RenderBox's fragment shader
# paired with uber_vertex (phi { <4 x half>, i8 }); and a callee's early return lowered BEFORE it is inlined, as
# nir_inline_function_impl requires (an assertion a release Mesa does not have). Inlined unlowered, that return became
# a return from the whole shader: B1L-0188's crash in bi_emit_jump; B1L-0190 lowered it after inlining, so the shader
# ended early without writing its colour. Checked on the Mac with the modules of B1L-0225 (b2l-bicomp, assertions on):
# fa16dhmha4 compiles, 93552 bytes of Bifrost code, 64 registers; QuartzCore's NIR byte for byte that of B1L-0226.
t = sys.argv[1] + '/air_to_nir.c'
u = open(t).read()
def rep227(a, b, what):
    global u
    if u.count(a) != 1:
        sys.exit('air-b2l: %s is not as expected (B1L-0227)' % what)
    u = u.replace(a, b)

rep227("""static bool
resolve_pointer_phi(struct air_ctx *c, const struct air_inst *in, const bool *reachable)
{""", """/* B2L (B1L-0227): does this function hold a return jump anywhere */
static bool
b2l_has_return(nir_function_impl *impl)
{
   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type == nir_instr_type_jump && nir_instr_as_jump(instr)->type == nir_jump_return)
            return true;
      }
   }
   return false;
}

/* B2L (B1L-0188): a phi of an aggregate is a tree of registers, one per leaf */
static struct air_val *
b2l_phi_agg_regs(struct air_ctx *c, nir_builder *entry, const struct air_type *t)
{
   if (t->kind == AIR_TYPE_STRUCT || t->kind == AIR_TYPE_ARRAY) {
      unsigned n = t->kind == AIR_TYPE_STRUCT ? t->num_members : (unsigned)t->count;
      struct air_val *v = air_val_agg(c, n);
      for (unsigned i = 0; i < n; i++) {
         v->elems[i] = b2l_phi_agg_regs(c, entry, t->kind == AIR_TYPE_STRUCT ? t->members[i] : t->elem);
         if (!v->elems[i])
            return NULL;
      }
      return v;
   }
   if (t->kind == AIR_TYPE_POINTER) {
      air_error(c, "instruction: a phi of an aggregate that holds a pointer");
      return NULL;
   }
   return air_val_ssa(c, nir_decl_reg(entry, air_nir_components(t), air_nir_bit_size(c, t), 0));
}

static struct air_val *
b2l_phi_agg_load(struct air_ctx *c, const struct air_val *regs)
{
   if (regs->kind != AIR_VAL_AGG)
      return air_val_ssa(c, nir_load_reg(&c->b, regs->def));
   struct air_val *v = air_val_agg(c, regs->num_elems);
   for (unsigned i = 0; i < regs->num_elems; i++)
      v->elems[i] = b2l_phi_agg_load(c, regs->elems[i]);
   return v;
}

static bool
b2l_phi_agg_store(struct air_ctx *c, const struct air_val *regs, const struct air_val *val)
{
   if (!val || val->kind == AIR_VAL_NONE)
      return true;   /* an undef member: nothing to carry */
   if (regs->kind != AIR_VAL_AGG) {
      if (val->kind != AIR_VAL_SSA || !val->def ||
          val->def->num_components != nir_intrinsic_num_components(nir_reg_get_decl(regs->def)))
         return air_error(c, "instruction: a phi of an aggregate one of whose leaves is not the value its type says");
      nir_store_reg(&c->b, val->def, regs->def);
      return true;
   }
   if (val->kind != AIR_VAL_AGG || val->num_elems != regs->num_elems)
      return air_error(c, "instruction: a phi of an aggregate whose operand is not that aggregate");
   for (unsigned i = 0; i < regs->num_elems; i++)
      if (!b2l_phi_agg_store(c, regs->elems[i], val->elems[i]))
         return false;
   return true;
}

static bool
resolve_pointer_phi(struct air_ctx *c, const struct air_inst *in, const bool *reachable)
{""", 'resolve_pointer_phi')

rep227("""         if (in->base.type->kind == AIR_TYPE_STRUCT ||
             in->base.type->kind == AIR_TYPE_ARRAY)
            return air_error(c, "instruction: a phi of %s, which NIR has no register for",
                             in->base.type->kind == AIR_TYPE_STRUCT ? "a struct"
                                                                    : "an array");
""", """         if (in->base.type->kind == AIR_TYPE_STRUCT ||
             in->base.type->kind == AIR_TYPE_ARRAY) {   /* B2L (B1L-0188): a tree of registers */
            struct air_val *regs = b2l_phi_agg_regs(c, &entry, in->base.type);
            if (!regs)
               return false;
            _mesa_hash_table_insert(c->phi_regs, in, regs);
            continue;
         }
""", 'the aggregate phi refusal')

rep227("""         struct hash_entry *e = _mesa_hash_table_search(c->phi_regs, in);
         air_set(c, &in->base,
                 air_val_ssa(c, nir_load_reg(&c->b, e->data)));""", """         struct hash_entry *e = _mesa_hash_table_search(c->phi_regs, in);
         if (in->base.type->kind == AIR_TYPE_STRUCT || in->base.type->kind == AIR_TYPE_ARRAY) {
            air_set(c, &in->base, b2l_phi_agg_load(c, e->data));   /* B2L (B1L-0188) */
            continue;
         }
         air_set(c, &in->base,
                 air_val_ssa(c, nir_load_reg(&c->b, e->data)));""", 'the phi load')

rep227("""            if (in->base.type->kind == AIR_TYPE_POINTER) {   /* B2L (B1L-0188): the offset of the same resource */""",
       """            if (in->base.type->kind == AIR_TYPE_STRUCT || in->base.type->kind == AIR_TYPE_ARRAY) {
               struct air_val *av = air_get(c, in->operands[k]);   /* B2L (B1L-0188) */
               if (!av || !b2l_phi_agg_store(c, re->data, av))
                  return false;
               *(nir_cursor *)ce->data = c->b.cursor;
               continue;
            }
            if (in->base.type->kind == AIR_TYPE_POINTER) {   /* B2L (B1L-0188): the offset of the same resource */""",
       'the phi store (B1L-0226)')

rep227("""   if (_mesa_hash_table_num_entries(c.fns)) {
      NIR_PASS(_, c.nir, nir_inline_functions);""", """   if (_mesa_hash_table_num_entries(c.fns)) {
      /* B2L (B1L-0227): a callee's early return becomes structured flow first, as nir_inline_function_impl requires;
       * inlined as a jump it returned from the whole shader (B1L-0188's crash in bi_emit_jump, B1L-0190's early exit).
       * Only a function that has a return: the pass reshapes even one without (QuartzCore's NIR, which has none, stays
       * byte for byte that of B1L-0226); fa16dhmha4's entry point has one of its own too, which Bifrost cannot emit */
      nir_foreach_function_impl(b2l_impl, c.nir) {
         if (b2l_has_return(b2l_impl))
            nir_lower_returns_impl(b2l_impl);
      }
      NIR_PASS(_, c.nir, nir_inline_functions);""", 'the inlining')
open(t, 'w').write(u)
print('air-b2l: phis of structs translated, early returns lowered before inlining (B1L-0227)')

# B1L-0228: a texture or sampler the shader DECLARES but the draw leaves unbound is not a refusal. RenderBox's
# fa16dhmha4 declares fill_tex, shape_tex, custom_tex and noise_tex and samples only those its data selects; Metal lets
# the others stay unbound, and this library refused the whole draw instead ("fa16dhmha4 reads texture 0 and 0 textures
# were bound", 34 draws of SpringBoard's in B1L-0227, where nothing of RenderBox's reached the screen). An unbound
# sampled texture now goes to Gallium as a NULL view, which panfrost emits as its null texture descriptor
# (panfrost_emit_null_texture); an unbound sampler as the device's default sampler. A WRITTEN image still has to be bound.
q = sys.argv[1] + '/air_metal.c'
u = open(q).read()
def rep228(a, b, what):
    global u
    if u.count(a) != 1:
        sys.exit('air-b2l: %s is not as expected (B1L-0228)' % what)
    u = u.replace(a, b)
rep228("""         if (!member && (r->index >= bindings->texture_count ||
                         !bindings->textures[r->index])) {
            say(why, why_size, "%s %s texture %u and %u texture%s bound", name,""",
       """         if (!member && !stored && (r->index >= bindings->texture_count ||
                                    !bindings->textures[r->index]))
            break;   /* B2L (B1L-0228): sampled and unbound: the null texture */
         if (!member && (r->index >= bindings->texture_count ||
                         !bindings->textures[r->index])) {
            say(why, why_size, "%s %s texture %u and %u texture%s bound", name,""", 'the texture check')
rep228("""         if (r->index >= bindings->sampler_count || !bindings->samplers[r->index]) {
            say(why, why_size, "%s takes sampler %u from the API and %u sampler%s bound",""",
       """         if (r->index >= bindings->sampler_count || !bindings->samplers[r->index])
            break;   /* B2L (B1L-0228): unbound: the default sampler (bind_stage) */
         if (r->index >= bindings->sampler_count || !bindings->samplers[r->index]) {
            say(why, why_size, "%s takes sampler %u from the API and %u sampler%s bound",""", 'the sampler check')
rep228("""         case AIR_RESOURCE_TEXTURE:
         case AIR_RESOURCE_IMAGE:
            t = bindings->textures[r->index];
            break;""", """         case AIR_RESOURCE_TEXTURE:
         case AIR_RESOURCE_IMAGE:
            t = r->index < bindings->texture_count ? bindings->textures[r->index] : NULL;   /* B2L (B1L-0228) */
            break;""", 'the texture lookup')
rep228("""         if (!t->view) {
            struct pipe_sampler_view tmpl = { 0 };""", """         if (!t) {   /* B2L (B1L-0228): unbound: a NULL view, panfrost's null texture */
            views[r->index] = NULL;
            num_views = MAX2(num_views, r->index + 1);
            break;
         }
         if (!t->view) {
            struct pipe_sampler_view tmpl = { 0 };""", 'the sampler view')
rep228("""            samplers[r->index] = (compare_sampler && compare_sampler[r->index])
                                    ? sm->cso_compare : sm->cso;""", """            if (!sm) {   /* B2L (B1L-0228): unbound: the device's default sampler */
               samplers[r->index] = default_sampler(device);
               if (!samplers[r->index]) {
                  say(why, why_size, "%s would not make a sampler at all", rwa_device_name(device));
                  return -1;
               }
            } else
            samplers[r->index] = (compare_sampler && compare_sampler[r->index])
                                    ? sm->cso_compare : sm->cso;""", 'the API sampler')
rep228("""            if (!r->inline_sampler)
               sm = bindings->samplers[r->index];""", """            if (!r->inline_sampler && r->index < bindings->sampler_count)   /* B2L (B1L-0228) */
               sm = bindings->samplers[r->index];""", 'the sampler lookup')
open(q, 'w').write(u)
print('air-b2l: unbound sampled textures and samplers bound as null/default (B1L-0228)')

# B1L-0229: the size of an UNBOUND texture is zero, not a read through a NULL view. B1L-0228 let a draw leave a sampled
# texture unbound (panfrost's null texture); fa16dhmha4 also asks its textures' sizes (air.get_width/height_texture_2d,
# panfrost's TEXTURE_SIZE sysval), and panfrost_upload_txs_sysval dereferenced the empty slot: SpringBoard crashed in
# panfrost_emit_const_buf+0x1f4 on every boot of B1L-0228 (frozen at the lock screen). Metal gives 0 for a texture that
# is not there; so does this now.
q = sys.argv[1] + '/../../gallium/drivers/panfrost/pan_cmdstream.c'
u = open(q).read()
if 'B1L-0229' not in u:
    a = """   struct pipe_sampler_view *tex = &ctx->sampler_views[st][texidx]->base;

   assert(dim);
"""
    if u.count(a) != 1:
        sys.exit('air-b2l: panfrost_upload_txs_sysval is not as expected (B1L-0229)')
    u = u.replace(a, """   if (!ctx->sampler_views[st][texidx]) {   /* B2L (B1L-0229): unbound: a size of zero */
      uniform->i[0] = uniform->i[1] = uniform->i[2] = 0;
      return;
   }
""" + a)
    open(q, 'w').write(u)
print('air-b2l: the size of an unbound texture is zero (B1L-0229)')

# B1L-0230: the shader's sampler count covers the samplers it USES, not only its textures. pan_shader.c set
# sampler_count = texture_count = last texture used + 1; Metal's texture and sampler numbers are apart, and RenderBox's
# fa16dhmha4 reads 4 textures through samplers 0..13 (inline samplers). Bifrost checks the index against the count of the
# renderer state, so every draw of fa16dhmha4 failed with 0x5A (ADDR_RANGE_FAULT, mali_kbase_mmu.c of Samsung's
# b_r16p0) in B1L-0229, the first build where it reached the GPU: no icons, freezes. This Mesa's nir_gather_info does
# not fill samplers_used, so the tex instructions are read here.
q = sys.argv[1] + '/../../panfrost/lib/pan_shader.c'
u = open(q).read()
if 'B1L-0230' not in u:
    a = """   info->sampler_count = info->texture_count =
      BITSET_LAST_BIT(s->info.textures_used);
"""
    if u.count(a) != 1:
        sys.exit('air-b2l: pan_shader.c sampler count is not as expected (B1L-0230)')
    u = u.replace(a, a + """   nir_foreach_function_impl(b2l_impl, s) {   /* B2L (B1L-0230): samplers numbered apart from textures */
      nir_foreach_block(b2l_block, b2l_impl) {
         nir_foreach_instr(b2l_instr, b2l_block) {
            if (b2l_instr->type == nir_instr_type_tex)
               info->sampler_count = MAX2(info->sampler_count,
                                          nir_instr_as_tex(b2l_instr)->sampler_index + 1);
         }
      }
   }
""")
    open(q, 'w').write(u)
print('air-b2l: a shader counts the samplers it uses (B1L-0230)')

# B1L-0232: MTLPixelFormatRGB10A2Unorm (90). SpringBoard (122x122, over a buffer) and iconservicesagent ask for it in
# B1L-0231 and the table refused the whole packed-32 family as unread. Its layout is fixed by another standard:
# MoltenVK (Apache-2.0, MoltenVK/GPUObjects/MVKPixelFormats.mm) pairs it with VK_FORMAT_A2B10G10R10_UNORM_PACK32,
# whose red is in bits 0-9 and alpha in 30-31, which is Gallium's PIPE_FORMAT_R10G10B10A2_UNORM (first channel at the
# least significant bits). panfrost samples and renders it (pan_format.c, RGB10_A2_UNORM, VTR). 91-94 stay refused.
q = sys.argv[1] + '/air_metal.c'
u = open(q).read()
if 'B1L-0232' not in u:
    a = """   {  81, "MTLPixelFormatBGRA8Unorm_sRGB", PIPE_FORMAT_B8G8R8A8_SRGB },
"""
    if u.count(a) != 1:
        sys.exit('air-b2l: the pixel format table is not as expected (B1L-0232)')
    u = u.replace(a, a + """   {  90, "MTLPixelFormatRGB10A2Unorm",    PIPE_FORMAT_R10G10B10A2_UNORM },   /* B2L (B1L-0232): MoltenVK's A2B10G10R10_UNORM_PACK32 */
""")
    open(q, 'w').write(u)
print('air-b2l: MTLPixelFormatRGB10A2Unorm (B1L-0232)')

# B1L-0234: a function the pipeline LINKS. RenderBox's custom effects (custom_effect_fragment and the primitive_custom
# shaders, 38 SpringBoard pipelines refused in B1L-0232) call `custom_fn.MTL_VISIBLE_FN_REF`, which the module only
# declares (air.visible_function_reference custom_fn); B1L-0233 showed what Metal links for it: one AIR module in
# MTLRenderPipelineDescriptor.fragmentLinkedFunctions.privateFunctions, the visible function custom_fn. The linked
# modules are read with the shader's, their globals and defined functions join its own, and a call of a declaration
# named <f>.MTL_VISIBLE_FN_REF is a call of the defined <f> -- the static linking Metal's compiler does.
# The driver hands the modules over with rwa_b2l_set_linked() before rwa_render_create.
q = sys.argv[1] + '/nir_air.h'
u = open(q).read()
if 'B1L-0234' not in u:
    a = """   bool library_wrapper;
};"""
    if u.count(a) != 1:
        sys.exit('air-b2l: air_to_nir_options is not as expected (B1L-0234)')
    u = u.replace(a, """   bool library_wrapper;

   /* B2L (B1L-0234): modules linked into this one (MTLLinkedFunctions) */
   const void *const *b2l_linked;
   const size_t *b2l_linked_size;
   unsigned b2l_num_linked;
};""")
    open(q, 'w').write(u)
q = sys.argv[1] + '/air_to_nir.c'
u = open(q).read()
if 'B1L-0234' not in u:
    a = """   c.m = air_module_read(ctx, bitcode, size, &read_error);
   if (!c.m) {
      if (error)
         *error = ralloc_asprintf(mem_ctx, "reader: %s", read_error);
      ralloc_free(ctx);
      return NULL;
   }
"""
    if u.count(a) != 1:
        sys.exit('air-b2l: air_to_nir module read is not as expected (B1L-0234)')
    u = u.replace(a, a + """   /* B2L (B1L-0234): the linked modules' globals and defined functions join this module's */
   for (unsigned l = 0; options && l < options->b2l_num_linked; l++) {
      const struct air_module *lm = air_module_read(ctx, options->b2l_linked[l], options->b2l_linked_size[l],
                                                    &read_error);
      if (!lm) {
         if (error)
            *error = ralloc_asprintf(mem_ctx, "reader: linked function %u: %s", l, read_error);
         ralloc_free(ctx);
         return NULL;
      }
      struct air_module *mm = (struct air_module *)c.m;
      struct air_global **g = ralloc_array(ctx, struct air_global *, mm->num_globals + lm->num_globals + 1);
      memcpy(g, mm->globals, mm->num_globals * sizeof(*g));
      memcpy(g + mm->num_globals, lm->globals, lm->num_globals * sizeof(*g));
      mm->globals = g;
      mm->num_globals += lm->num_globals;
      struct air_function **f = ralloc_array(ctx, struct air_function *, mm->num_functions + lm->num_functions + 1);
      memcpy(f, mm->functions, mm->num_functions * sizeof(*f));
      unsigned nf = mm->num_functions;
      for (unsigned i = 0; i < lm->num_functions; i++)
         if (!lm->functions[i]->is_declaration && lm->functions[i]->num_blocks)
            f[nf++] = lm->functions[i];
      mm->functions = f;
      mm->num_functions = nf;
   }
""")
    a = """      if (fname && !strcmp(fname, name)) {
         f = c->m->functions[i];
         break;
      }
   }
"""
    if u.count(a) != 1:
        sys.exit('air-b2l: air_call_in_module is not as expected (B1L-0234)')
    u = u.replace(a, a + """   /* B2L (B1L-0234): a visible function's reference is a call of the function a linked module defines */
   static const char b2l_ref[] = ".MTL_VISIBLE_FN_REF";
   size_t b2l_nl = strlen(name), b2l_rl = sizeof(b2l_ref) - 1;
   if (f && f->is_declaration && b2l_nl > b2l_rl && !strcmp(name + b2l_nl - b2l_rl, b2l_ref)) {
      for (unsigned i = 0; i < c->m->num_functions; i++) {
         const char *fname = c->m->functions[i]->base.name;
         if (fname && strlen(fname) == b2l_nl - b2l_rl && !strncmp(fname, name, b2l_nl - b2l_rl) &&
             !c->m->functions[i]->is_declaration && c->m->functions[i]->num_blocks) {
            f = c->m->functions[i];
            break;
         }
      }
   }
""")
    open(q, 'w').write(u)
q = sys.argv[1] + '/air_metal.c'
u = open(q).read()
if 'B1L-0234' not in u:
    a = """struct rwa_render *
rwa_render_create(struct rwa_device *device,"""
    if u.count(a) != 1:
        sys.exit('air-b2l: rwa_render_create is not as expected (B1L-0234)')
    u = u.replace(a, """/* B2L (B1L-0234): the modules the next rwa_render_create links into its fragment (stage 1) or vertex (stage 0)
 * function, from MTLRenderPipelineDescriptor's linked functions. Kept by pointer until the driver clears them (n = 0)
 * right after that call; one thread's at a time. */
static __thread const void *const *b2l_linked_bc[2];
static __thread const size_t *b2l_linked_sz[2];
static __thread unsigned b2l_linked_n[2];
void rwa_b2l_set_linked(unsigned stage, const void *const *bitcode, const size_t *size, unsigned n);
void
rwa_b2l_set_linked(unsigned stage, const void *const *bitcode, const size_t *size, unsigned n)
{
   if (stage > 1)
      return;
   b2l_linked_bc[stage] = n ? bitcode : NULL;
   b2l_linked_sz[stage] = n ? size : NULL;
   b2l_linked_n[stage] = n;
}

""" + a)
    a = "   const struct air_to_nir_options fopts = { .constants = ffc, .num_constants = num_ffc };"
    if u.count(a) != 1:
        sys.exit('air-b2l: rwa_render_create fopts is not as expected (B1L-0234)')
    u = u.replace(a, """   const struct air_to_nir_options fopts = { .constants = ffc, .num_constants = num_ffc,
                                             .b2l_linked = b2l_linked_bc[1], .b2l_linked_size = b2l_linked_sz[1],
                                             .b2l_num_linked = b2l_linked_n[1] };   /* B2L (B1L-0234) */""")
    a = "      const struct air_to_nir_options vopts = { .constants = vfc, .num_constants = num_vfc };"
    if u.count(a) != 1:
        sys.exit('air-b2l: rwa_render_create vopts is not as expected (B1L-0234)')
    u = u.replace(a, """      const struct air_to_nir_options vopts = { .constants = vfc, .num_constants = num_vfc,
                                                .b2l_linked = b2l_linked_bc[0], .b2l_linked_size = b2l_linked_sz[0],
                                                .b2l_num_linked = b2l_linked_n[0] };   /* B2L (B1L-0234) */""")
    open(q, 'w').write(u)
print('air-b2l: functions a render pipeline links (B1L-0234)')

# B1L-0244: air.write_texture_* took its NIR src_type from the instruction's own return type, which is void for a write,
# so tex_dest_type fell back to "uint": a float4 written to an RGBA8Unorm texture by a compute kernel was stored as the
# float's raw bits converted from uint, every channel saturated (0xffffffff in the Test Metal app, B1L-0242). The type
# of the written VALUE (float, half, or int with the .s/.u in the name) is what the store carries.
i = sys.argv[1] + '/air_intrinsics.c'
u = open(i).read()
if 'B1L-0244' not in u:
    a = """      nir_def *value = air_get_ssa(c, in->operands[arg++]);
      if (!value)
         return false;"""
    b = """      const struct air_type *value_type = in->operands[arg]->type;   /* B2L (B1L-0244) */
      nir_def *value = air_get_ssa(c, in->operands[arg++]);
      if (!value)
         return false;
      if (value_type)
         dest_type = tex_dest_type(c, value_type, n);"""
    if u.count(a) != 1:
        sys.exit('air-b2l: the write_texture value is not where B1L-0244 expects it')
    u = u.replace(a, b)
    open(i, 'w').write(u)
print('air-b2l: write_texture stores with the type of the value written (B1L-0244)')


# B1L-0245: a multisample resolve. rwa_texture_copy refused any multisample texture; Gallium's blit resolves when the
# source has several samples and the destination one, which is what -[MTLRenderPassColorAttachmentDescriptor
# resolveTexture] asks at the end of a pass. A multisample DESTINATION is still refused.
q = sys.argv[1] + '/air_metal.c'
u = open(q).read()
if 'B1L-0245' not in u:
    a = """   if (src->res->nr_samples > 1 || dst->res->nr_samples > 1) {
      say(why, why_size, "a copy between multisample textures is not implemented");
      return -1;
   }"""
    b = """   if (dst->res->nr_samples > 1) {   /* B2L (B1L-0245): a source of several samples is a resolve, done by the blit */
      say(why, why_size, "a copy into a multisample texture is not implemented");
      return -1;
   }"""
    if u.count(a) != 1:
        sys.exit('air-b2l: rwa_texture_copy multisample refusal is not as expected (B1L-0245)')
    u = u.replace(a, b)
    open(q, 'w').write(u)
print('air-b2l: rwa_texture_copy resolves a multisample source (B1L-0245)')

# B1L-0257: a pointer stored into a local (alloca) at a constant offset is kept as a translation-time slot and given
# back by a load of it (RenderBox custom_fn keeps its texture in a RB::Layer local); the module reader skips record 20
# (GUIDLIST, newer LLVM); air2nir links B2L_LINK=<file.air> as the device does (host test only).
def b2l_0257(name, pairs):
    q = sys.argv[1] + "/" + name
    u = open(q).read()
    if "B1L-0257" in u:
        return
    for a, b in pairs:
        if u.count(a) != 1:
            sys.exit("air-b2l: %s is not as expected (B1L-0257)" % name)
        u = u.replace(a, b)
    open(q, "w").write(u)
b2l_0257('air_to_nir.c', [('ptr_offset(struct air_ctx *c, struct air_ptr ptr)\n{\n   return ptr.offset ? ptr.offset : nir_imm_int(&c->b, 0);\n}\n\nnir_def *\n', 'ptr_offset(struct air_ctx *c, struct air_ptr ptr)\n{\n   return ptr.offset ? ptr.offset : nir_imm_int(&c->b, 0);\n}\n\n/* B2L (B1L-0257): the constant byte offset of a local, or false when it is not a local at a constant offset */\nstatic bool\nb2l_scratch_const_offset(struct air_ctx *c, const struct air_val *p, uint64_t *off)\n{\n   if (!p || p->kind != AIR_VAL_PTR || p->ptr.class != AIR_PTR_SCRATCH)\n      return false;\n   nir_def *o = ptr_offset(c, p->ptr);\n   if (!o || !nir_src_is_const(nir_src_for_ssa(o)))\n      return false;\n   *off = nir_src_as_uint(nir_src_for_ssa(o));\n   return true;\n}\n\nstatic struct b2l_ptr_slot *\nb2l_find_ptr_slot(struct air_ctx *c, uint64_t off)\n{\n   for (unsigned i = 0; i < c->b2l_num_ptr_slots; i++)\n      if (c->b2l_ptr_slots[i].offset == off)\n         return &c->b2l_ptr_slots[i];\n   return NULL;\n}\n\nnir_def *\n'), ('      }\n      if (p->kind != AIR_VAL_PTR)\n         return air_error(c, "instruction: a load from something that is not a pointer");\n      /* An argument buffer resolves before it loads: the byte offset names a\n       * member and the member\'s descriptor names a binding, so this load\n       * becomes a pointer and not an instruction.\n', '      }\n      if (p->kind != AIR_VAL_PTR)\n         return air_error(c, "instruction: a load from something that is not a pointer");\n      /* B2L (B1L-0257): a pointer read back out of a local: the slot a store kept */\n      if (in->base.type && in->base.type->kind == AIR_TYPE_POINTER && p->ptr.class == AIR_PTR_SCRATCH) {\n         uint64_t off;\n         struct b2l_ptr_slot *slot = b2l_scratch_const_offset(c, p, &off) ? b2l_find_ptr_slot(c, off) : NULL;\n         if (!slot)\n            return air_error(c, "instruction: a pointer loaded from a local nothing stored a pointer to");\n         if (slot->conflict)\n            return air_error(c, "instruction: a pointer loaded from a local two different pointers were stored to");\n         air_set(c, &in->base, slot->val);\n         return true;\n      }\n      /* An argument buffer resolves before it loads: the byte offset names a\n       * member and the member\'s descriptor names a binding, so this load\n       * becomes a pointer and not an instruction.\n'), ('      struct air_val *sv = air_get(c, in->operands[0]);\n      if (!sv)\n         return false;\n      if (val_is_pointerish(sv))\n         return air_error(c, "instruction: a store of a pointer into memory, which this "\n                             "frontend has no address to write");\n      if (p->kind == AIR_VAL_PTR_SELECT)\n         return air_error(c, "instruction: a store through a chosen pointer, which would "\n                             "have to write both of them");\n', '      struct air_val *sv = air_get(c, in->operands[0]);\n      if (!sv)\n         return false;\n      if (val_is_pointerish(sv)) {\n         /* B2L (B1L-0257): into a local at a constant offset, the pointer is kept as a translation-time\n          * slot (RenderBox\'s custom_fn keeps its texture in a RB::Layer local and reads it back). */\n         uint64_t off;\n         if (!b2l_scratch_const_offset(c, p, &off))\n            return air_error(c, "instruction: a store of a pointer into memory, which this "\n                                "frontend has no address to write");\n         struct b2l_ptr_slot *slot = b2l_find_ptr_slot(c, off);\n         if (!slot) {\n            c->b2l_ptr_slots = reralloc(c->mem_ctx, c->b2l_ptr_slots, struct b2l_ptr_slot,\n                                        c->b2l_num_ptr_slots + 1);\n            slot = &c->b2l_ptr_slots[c->b2l_num_ptr_slots++];\n            *slot = (struct b2l_ptr_slot){ .offset = off, .val = sv };\n         } else if (slot->val != sv && !(sv->kind == AIR_VAL_PTR && slot->val->kind == AIR_VAL_PTR &&\n                                         same_ptr(&sv->ptr, &slot->val->ptr))) {\n            slot->conflict = true;   /* two pointers: which one a load sees is a run-time question */\n         }\n         return true;\n      }\n      if (p->kind == AIR_VAL_PTR_SELECT)\n         return air_error(c, "instruction: a store through a chosen pointer, which would "\n                             "have to write both of them");\n')])
b2l_0257('air_private.h', [('   unsigned scratch_size;\n   unsigned shared_size;\n\n   /* module-scope variables, by their air_global */\n   struct hash_table *globals;\n\n', '   unsigned scratch_size;\n   unsigned shared_size;\n\n   /* B2L (B1L-0257): pointers stored into a local (alloca) at a constant offset, kept as the resolution they\n    * are, since a pointer here has no bytes to write. `conflict` = two different pointers were stored there. */\n   struct b2l_ptr_slot {\n      uint64_t offset;\n      struct air_val *val;\n      bool conflict;\n   } *b2l_ptr_slots;\n   unsigned b2l_num_ptr_slots;\n\n   /* module-scope variables, by their air_global */\n   struct hash_table *globals;\n\n')])
b2l_0257('air_module.c', [('      case MODULE_CODE_VSTOFFSET:\n      case MODULE_CODE_HASH:\n      case MODULE_CODE_GCNAME:\n         break;\n      default:\n         return fail(r, "unsupported module record %u", e.id);\n', '      case MODULE_CODE_VSTOFFSET:\n      case MODULE_CODE_HASH:\n      case MODULE_CODE_GCNAME:\n      case 20:   /* B2L (B1L-0257): GUIDLIST, newer LLVM\'s per-function GUIDs (for ThinLTO): nothing for a shader */\n         break;\n      default:\n         return fail(r, "unsupported module record %u", e.id);\n')])
b2l_0257('air2nir.c', [('      unsigned num_fc = air_module_function_constants(ctx, mods[k].bitcode, mods[k].size, &fc);\n      struct air_to_nir_options options = { .constants = fc, .num_constants = num_fc,\n                                        .library_wrapper = library_wrapper };\n      struct air_shader_info info;\n      nir_shader *nir = air_to_nir(ctx, mods[k].bitcode, mods[k].size, NULL, &options,\n                                   &default_options, &info, &err);\n', '      unsigned num_fc = air_module_function_constants(ctx, mods[k].bitcode, mods[k].size, &fc);\n      struct air_to_nir_options options = { .constants = fc, .num_constants = num_fc,\n                                        .library_wrapper = library_wrapper };\n      /* B2L (B1L-0257): B2L_LINK=file.air links that module in, as rwa_b2l_set_linked does on the device */\n      const char *link_path = getenv("B2L_LINK");\n      const void *link_bc[1];\n      size_t link_sz[1];\n      if (link_path) {\n         FILE *lf = fopen(link_path, "rb");\n         if (lf) {\n            fseek(lf, 0, SEEK_END);\n            link_sz[0] = ftell(lf);\n            fseek(lf, 0, SEEK_SET);\n            void *lb = ralloc_size(ctx, link_sz[0]);\n            if (fread(lb, 1, link_sz[0], lf) == link_sz[0]) {\n               link_bc[0] = lb;\n               options.b2l_linked = link_bc;\n               options.b2l_linked_size = link_sz;\n               options.b2l_num_linked = 1;\n            }\n            fclose(lf);\n         }\n      }\n      struct air_shader_info info;\n      nir_shader *nir = air_to_nir(ctx, mods[k].bitcode, mods[k].size, NULL, &options,\n                                   &default_options, &info, &err);\n')])
print('air-b2l: pointers kept in locals, GUIDLIST skipped, air2nir B2L_LINK (B1L-0257)')

# B1L-0261: Metal's primitive restart. -drawIndexedPrimitives: always restarts a strip at the all-ones index (0xFFFF for
# MTLIndexTypeUInt16, 0xFFFFFFFF for UInt32, Apple's documentation of the call). RenderBox batches its quads as
# "1 0 2 3 65535 5 4 6 7 65535 ..." strips; with restart off, 65535 was drawn as a vertex: slivers across the icon
# (the streaks) and half-quads (the pictograms cut along a diagonal, B1L-0260). The bounds walk skips it too.
def b2l_0261(name, pairs):
    q = sys.argv[1] + "/" + name
    u = open(q).read()
    if "B1L-0261" in u:
        return
    for a, b in pairs:
        if u.count(a) != 1:
            sys.exit("air-b2l: %s is not as expected (B1L-0261)" % name)
        u = u.replace(a, b)
    open(q, "w").write(u)
b2l_0261('air_metal.c', [
('''   unsigned min = UINT32_MAX, max = 0;
   for (unsigned i = 0; i < count; i++) {
      unsigned v;
      if (index_size == 2) {
         uint16_t u;
         memcpy(&u, indices + (size_t)i * 2, sizeof(u));
         v = u;
      } else {
         uint32_t u;
         memcpy(&u, indices + (size_t)i * 4, sizeof(u));
         v = u;
      }
      if (v < min)
         min = v;
      if (v > max)
         max = v;
   }
   *min_out = min;
   *max_out = max;''',
'''   unsigned min = UINT32_MAX, max = 0;
   const unsigned restart = index_size == 2 ? 0xffffu : 0xffffffffu;   /* B2L (B1L-0261): not a vertex */
   for (unsigned i = 0; i < count; i++) {
      unsigned v;
      if (index_size == 2) {
         uint16_t u;
         memcpy(&u, indices + (size_t)i * 2, sizeof(u));
         v = u;
      } else {
         uint32_t u;
         memcpy(&u, indices + (size_t)i * 4, sizeof(u));
         v = u;
      }
      if (v == restart)
         continue;
      if (v < min)
         min = v;
      if (v > max)
         max = v;
   }
   if (min > max)   /* B2L (B1L-0261): nothing but restarts */
      min = max = 0;
   *min_out = min;
   *max_out = max;'''),
('''      struct pipe_draw_info info = {
         .mode = prim, .index_bounds_valid = true,
         .index_size = (uint16_t)index_size,''',
'''      /* B2L (B1L-0261): restart IS on for an indexed draw -- Metal always restarts at the all-ones index
       * (0xFFFF / 0xFFFFFFFF); the paragraph above is wrong about that. */
      struct pipe_draw_info info = {
         .mode = prim, .index_bounds_valid = true,
         .primitive_restart = index_size != 0,
         .restart_index = index_size == 2 ? 0xffffu : 0xffffffffu,
         .index_size = (uint16_t)index_size,'''),
])
print('air-b2l: indexed draws restart at the all-ones index, as Metal does (B1L-0261)')

# B1L-0263: a Metal buffer is mapped once and the caller writes through that pointer for the buffer's whole life,
# but the resource was made without PIPE_RESOURCE_FLAG_MAP_PERSISTENT. Then any later map of it by panfrost -- the
# copies and fills of rwa_buffer_copy / rwa_buffer_fill (util_resource_copy_region, u_default_clear_buffer) -- may
# swap in a new BO when a batch still reads it or the write covers it all (panfrost_ptr_map, pan_resource.c:
# create_new_bo / "Shadow resource creation"). From then on the caller's pointer is the dropped BO: what it writes
# never reaches the GPU (geometry from stale vertices: the streaks and triangles over icons after a while). With the
# persistent flag panfrost never swaps an already mapped BO; it flushes and waits instead (the same file, the
# "Synchronized write" branch), which is what Gallium's persistent mappings (ARB_buffer_storage) rely on.
def b2l_0263(name, pairs):
    q = sys.argv[1] + "/" + name
    u = open(q).read()
    if "B1L-0263" in u:
        return
    for a, b in pairs:
        if u.count(a) != 1:
            sys.exit("air-b2l: %s is not as expected (B1L-0263)" % name)
        u = u.replace(a, b)
    open(q, "w").write(u)
b2l_0263('air_metal.c', [
('''   buffer->res = pipe_buffer_create(device->screen,
                                    PIPE_BIND_SHADER_BUFFER | PIPE_BIND_CONSTANT_BUFFER |
                                    PIPE_BIND_INDEX_BUFFER,
                                    PIPE_USAGE_STAGING, size);''',
'''   /* B2L (B1L-0263): persistent and coherent, since the mapping below is kept for life (see apply.py) */
   struct pipe_resource b2l_tmpl = {
      .target = PIPE_BUFFER, .format = PIPE_FORMAT_R8_UNORM,
      .bind = PIPE_BIND_SHADER_BUFFER | PIPE_BIND_CONSTANT_BUFFER | PIPE_BIND_INDEX_BUFFER,
      .usage = PIPE_USAGE_STAGING,
      .flags = PIPE_RESOURCE_FLAG_MAP_PERSISTENT | PIPE_RESOURCE_FLAG_MAP_COHERENT,
      .width0 = (unsigned)size, .height0 = 1, .depth0 = 1, .array_size = 1,
   };
   buffer->res = device->screen->resource_create(device->screen, &b2l_tmpl);'''),
('''   buffer->map = pipe_buffer_map(device->pipe, buffer->res,
                                 PIPE_MAP_READ | PIPE_MAP_WRITE, &buffer->xfer);''',
'''   buffer->map = pipe_buffer_map(device->pipe, buffer->res,
                                 PIPE_MAP_READ | PIPE_MAP_WRITE | PIPE_MAP_PERSISTENT |
                                 PIPE_MAP_COHERENT, &buffer->xfer);   /* B2L (B1L-0263) */'''),
])
print('air-b2l: Metal buffers are persistent mappings, so panfrost never swaps the BO under the caller (B1L-0263)')

# B1L-0266: Metal's fragment [[position]] is the pixel CENTRE (x + 0.5, y + 0.5), and the frontend says so
# (pixel_center_integer = false). But panfrost on Bifrost and later reports pipe_caps.fs_coord_pixel_center_integer and
# not ..._half_integer (pan_screen.c: dev->arch >= 6): its load_frag_coord is the pixel corner, and the +0.5 is the
# caller's job (the note in nir_lower_frag_coord_to_pixel_coord.c; Gallium's GL state tracker adds it). Nothing here did:
# every Metal position on the Mali was half a pixel off (B1L-0265: every gradient one half step low), so RenderBox
# sampled its masks at texel corners. Freedreno reports half-integer centres, which is why the Adreno never showed it.
def b2l_0266(name, pairs):
    q = sys.argv[1] + "/" + name
    u = open(q).read()
    if "B1L-0266" in u:
        return
    for a, b in pairs:
        if u.count(a) != 1:
            sys.exit("air-b2l: %s is not as expected (B1L-0266)" % name)
        u = u.replace(a, b)
    open(q, "w").write(u)
b2l_0266('air_metal.c', [
('''static void
finalize_for_gallium(nir_shader *nir, struct pipe_screen *screen, unsigned ubo_base)
''',
'''/* B2L (B1L-0266): +0.5 on x and y of the fragment position, for a screen whose position is the pixel corner.
 * shortcut: the pixel centre also under per-sample shading, where Metal gives the sample position; do it when a
 * shader reading [[sample_id]] with [[position]] needs it. */
static bool
b2l_frag_coord_centre(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   if (intr->intrinsic != nir_intrinsic_load_frag_coord)
      return false;
   b->cursor = nir_after_instr(&intr->instr);
   nir_def *centred = nir_fadd(b, &intr->def, nir_imm_vec4(b, 0.5f, 0.5f, 0.0f, 0.0f));
   nir_def_rewrite_uses_after(&intr->def, centred, centred->parent_instr);
   return true;
}

static void
finalize_for_gallium(nir_shader *nir, struct pipe_screen *screen, unsigned ubo_base)
'''),
('''   NIR_PASS(_, nir, nir_lower_system_values);
''',
'''   NIR_PASS(_, nir, nir_lower_system_values);
   if (nir->info.stage == MESA_SHADER_FRAGMENT && screen->caps.fs_position_is_sysval &&
       screen->caps.fs_coord_pixel_center_integer && !screen->caps.fs_coord_pixel_center_half_integer)
      NIR_PASS(_, nir, nir_shader_intrinsics_pass, b2l_frag_coord_centre, nir_metadata_control_flow, NULL);   /* B2L (B1L-0266) */
'''),
])
print('air-b2l: the fragment position is the pixel centre on screens whose own is the corner (B1L-0266)')

# B1L-0268: the translation of a render pipeline's modules (AIR -> NIR) runs WITHOUT the driver's context lock.
# B1L-0267 measured -newRenderPipelineStateWithDescriptor: holding the one lock of the Gallium context for up to 5 s in
# 5 s (backboardd) and 1.6 s (SpringBoard), so every other thread's draws waited for a compile. The translation needs no
# context: air_to_nir builds NIR from bitcode (no mutable statics in the frontend; glsl types take their own
# glsl_type_cache_mutex), and the checks before it ask only the screen (is_format_supported). panfrost's compile does
# need it (panfrost_create_shader_state uses ctx->shaders and ctx->descs), so the lock is taken back before
# make_stage_cso. The driver hands the two calls over (rwa_set_context_lock); without them nothing changes.
def b2l_0268(name, pairs):
    q = sys.argv[1] + "/" + name
    u = open(q).read()
    if "B1L-0268" in u:
        return
    for a, b in pairs:
        if u.count(a) != 1:
            sys.exit("air-b2l: %s is not as expected (B1L-0268)" % name)
        u = u.replace(a, b)
    open(q, "w").write(u)
b2l_0268('air_metal.c', [
('''void rwa_b2l_set_linked(unsigned stage, const void *const *bitcode, const size_t *size, unsigned n);
''',
'''void rwa_b2l_set_linked(unsigned stage, const void *const *bitcode, const size_t *size, unsigned n);
/* B2L (B1L-0268): the driver's context lock, let go while a pipeline's modules are translated */
static void (*b2l_ctx_drop)(void), (*b2l_ctx_take)(void);
void rwa_set_context_lock(void (*drop)(void), void (*take)(void));
void
rwa_set_context_lock(void (*drop)(void), void (*take)(void))
{
   b2l_ctx_drop = drop;
   b2l_ctx_take = take;
}
'''),
('''   render->device = device;
   render->mem_ctx = ralloc_context(NULL);
   render->shape = *shape;
''',
'''   render->device = device;
   render->mem_ctx = ralloc_context(NULL);
   render->shape = *shape;
   /* B2L (B1L-0268): from here every way out is goto fail or past make_stage_cso, both of which take the
    * context back; nothing until make_stage_cso touches it (the translation asks only the screen) */
   bool b2l_dropped = b2l_ctx_drop && b2l_ctx_take;
   if (b2l_dropped)
      b2l_ctx_drop();
'''),
('''   b2l_ubo_read_ends(fs, &render->finfo);   /* B2L (B1L-0115) */
   if (!render->generated_vertex)
      b2l_ubo_read_ends(vs, &render->vinfo);
   render->fs_cso = make_stage_cso(device, fs, why, why_size);''',
'''   b2l_ubo_read_ends(fs, &render->finfo);   /* B2L (B1L-0115) */
   if (!render->generated_vertex)
      b2l_ubo_read_ends(vs, &render->vinfo);
   if (b2l_dropped) {   /* B2L (B1L-0268) */
      b2l_ctx_take();
      b2l_dropped = false;
   }
   render->fs_cso = make_stage_cso(device, fs, why, why_size);'''),
('''fail:
   {   /* B2L (B1L-0224): a refused pipeline, named, and its modules sent */''',
'''fail:
   if (b2l_dropped)   /* B2L (B1L-0268): rwa_render_destroy below needs the context */
      b2l_ctx_take();
   {   /* B2L (B1L-0224): a refused pipeline, named, and its modules sent */'''),
])
print('air-b2l: a render pipeline is translated without the context lock, compiled with it (B1L-0268)')

# B1L-0288 (G2, docs/journal/plan-gpu-jesvi.md): count panfrost's shader disk cache (B1L-0095) per process -- found, missing,
# no cache at all -- and the time spent reading it and compiling, so the 7 to 14 s of "shader compile" per boot
# (B2L HOLD, which also covers the AIR -> NIR translation this cache never holds) can be split. Measurement only.
q = sys.argv[1] + '/../../gallium/drivers/panfrost/pan_shader.c'
u = open(q).read()
if 'B1L-0288' not in u:
    for a, b in [
('''#include "pan_context.h"
''',
'''#include "pan_context.h"
#ifdef __APPLE__   /* B2L (B1L-0288) */
#include <stdio.h>
#include <string.h>
#include <sys/sysctl.h>
#include "util/os_time.h"
static unsigned b2l_sc_hit, b2l_sc_miss, b2l_sc_none, b2l_sc_lines;
static uint64_t b2l_sc_hit_ns, b2l_sc_compile_ns, b2l_sc_longest_ns;
static void
b2l_sc_count(int what, uint64_t ns)   /* 0 found, 1 compiled, 2 compiled without a cache */
{
   unsigned *n = what == 0 ? &b2l_sc_hit : what == 1 ? &b2l_sc_miss : &b2l_sc_none;
   __atomic_add_fetch(n, 1, __ATOMIC_RELAXED);
   unsigned total = b2l_sc_hit + b2l_sc_miss + b2l_sc_none;
   __atomic_add_fetch(what == 0 ? &b2l_sc_hit_ns : &b2l_sc_compile_ns, ns, __ATOMIC_RELAXED);
   if (what != 0 && ns > b2l_sc_longest_ns)
      b2l_sc_longest_ns = ns;
   /* shortcut: racy reads of the other counters, good enough for a log line */
   if ((total <= 5 || total % 25 == 0 || (what != 0 && ns > 500000000ull)) && b2l_sc_lines < 80) {
      b2l_sc_lines++;
      char line[256];
      snprintf(line, sizeof(line), "B2L SHADERCACHE: %s %d: %u found (%llu ms), %u compiled, %u without cache "
               "(%llu ms compiling, longest %llu ms)", getprogname(), getpid(), b2l_sc_hit,
               (unsigned long long)(b2l_sc_hit_ns / 1000000), b2l_sc_miss, b2l_sc_none,
               (unsigned long long)(b2l_sc_compile_ns / 1000000), (unsigned long long)(b2l_sc_longest_ns / 1000000));
      (void)sysctlbyname("kern.hakios.say", NULL, NULL, line, strlen(line));
   }
}
#endif
'''),
('''   if (!panfrost_disk_cache_retrieve(screen->disk_cache, uncompiled,
                                     &state->key, &res)) {
      panfrost_shader_compile(screen, uncompiled->nir, dbg, &state->key,
                              req_local_mem, uncompiled->fixed_varying_mask,
                              &res);

      panfrost_disk_cache_store(screen->disk_cache, uncompiled, &state->key,
                                &res);
   }
''',
'''#ifdef __APPLE__
   uint64_t b2l_t0 = os_time_get_nano();   /* B2L (B1L-0288) */
#endif
   if (!panfrost_disk_cache_retrieve(screen->disk_cache, uncompiled,
                                     &state->key, &res)) {
      panfrost_shader_compile(screen, uncompiled->nir, dbg, &state->key,
                              req_local_mem, uncompiled->fixed_varying_mask,
                              &res);

      panfrost_disk_cache_store(screen->disk_cache, uncompiled, &state->key,
                                &res);
#ifdef __APPLE__
      b2l_sc_count(screen->disk_cache ? 1 : 2, os_time_get_nano() - b2l_t0);
   } else {
      b2l_sc_count(0, os_time_get_nano() - b2l_t0);
#endif
   }
'''),
    ]:
        if u.count(a) != 1:
            sys.exit('air-b2l: pan_shader.c is not as expected (B1L-0288)')
        u = u.replace(a, b)
    open(q, 'w').write(u)
print('air-b2l: panfrost shader cache found/compiled counted in the kernel log (B1L-0288)')

# B1L-0289 (G2): 0288 showed 456 shaders found in the cache for ~4.4 s, ~10 ms each. Split that time between the key
# (the NIR serialized and hashed, panfrost_disk_cache_compute_key) and the read (disk_cache_get: the database's locks,
# reads, and the access-time write it does on every hit), so the fix goes to the right one. Measurement only.
q = sys.argv[1] + '/../../gallium/drivers/panfrost/pan_disk_cache.c'
u = open(q).read()
if 'B1L-0289' not in u:
    for a, b in [
('''#include "util/mesa-sha1.h"
''',
'''#include "util/mesa-sha1.h"
#include "util/os_time.h"   /* B2L (B1L-0289) */
uint64_t b2l_sc_key_ns, b2l_sc_get_ns;   /* read by pan_shader.c's B2L SHADERCACHE line */
'''),
('''   cache_key cache_key;
   panfrost_disk_cache_compute_key(cache, uncompiled, key, cache_key);

   if (debug) {
      char sha1[41];
      _mesa_sha1_format(sha1, cache_key);
      fprintf(stderr, "[mesa disk cache] retrieving %s: ", sha1);
   }

   size_t size;
   void *buffer = disk_cache_get(cache, cache_key, &size);
''',
'''   cache_key cache_key;
   uint64_t b2l_t0 = os_time_get_nano();   /* B2L (B1L-0289) */
   panfrost_disk_cache_compute_key(cache, uncompiled, key, cache_key);
   uint64_t b2l_t1 = os_time_get_nano();
   __atomic_add_fetch(&b2l_sc_key_ns, b2l_t1 - b2l_t0, __ATOMIC_RELAXED);

   if (debug) {
      char sha1[41];
      _mesa_sha1_format(sha1, cache_key);
      fprintf(stderr, "[mesa disk cache] retrieving %s: ", sha1);
   }

   size_t size;
   void *buffer = disk_cache_get(cache, cache_key, &size);
   __atomic_add_fetch(&b2l_sc_get_ns, os_time_get_nano() - b2l_t1, __ATOMIC_RELAXED);
'''),
    ]:
        if u.count(a) != 1:
            sys.exit('air-b2l: pan_disk_cache.c is not as expected (B1L-0289)')
        u = u.replace(a, b)
    open(q, 'w').write(u)
q = sys.argv[1] + '/../../gallium/drivers/panfrost/pan_shader.c'
u = open(q).read()
if 'B1L-0289' not in u:
    for a, b in [
('''static unsigned b2l_sc_hit, b2l_sc_miss, b2l_sc_none, b2l_sc_lines;
''',
'''static unsigned b2l_sc_hit, b2l_sc_miss, b2l_sc_none, b2l_sc_lines;
extern uint64_t b2l_sc_key_ns, b2l_sc_get_ns;   /* B2L (B1L-0289): pan_disk_cache.c */
'''),
('''"(%llu ms compiling, longest %llu ms)", getprogname(), getpid(), b2l_sc_hit,
               (unsigned long long)(b2l_sc_hit_ns / 1000000), b2l_sc_miss, b2l_sc_none,
               (unsigned long long)(b2l_sc_compile_ns / 1000000), (unsigned long long)(b2l_sc_longest_ns / 1000000));''',
'''"(%llu ms compiling, longest %llu ms); key %llu ms, read %llu ms", getprogname(), getpid(), b2l_sc_hit,
               (unsigned long long)(b2l_sc_hit_ns / 1000000), b2l_sc_miss, b2l_sc_none,
               (unsigned long long)(b2l_sc_compile_ns / 1000000), (unsigned long long)(b2l_sc_longest_ns / 1000000),
               (unsigned long long)(b2l_sc_key_ns / 1000000), (unsigned long long)(b2l_sc_get_ns / 1000000));'''),
    ]:
        if u.count(a) != 1:
            sys.exit('air-b2l: pan_shader.c is not as expected (B1L-0289)')
        u = u.replace(a, b)
    open(q, 'w').write(u)
print('air-b2l: shader cache time split into key and read (B1L-0289)')

# B1L-0290 (G2): 0289 showed the cache's time is all in the read (key 2 ms, read 6716 ms for 425 lookups, ~16 ms
# each). Mesa's default database cache (util/mesa_cache_db*.c) is 50 parts; each lookup takes file locks, rereads
# headers, may walk several parts, and writes the access time back with fflush on every hit. Mesa's single-file
# cache (util/fossilize_db.c) keeps its index in memory: a hit is one fseek and fread under a mutex. Its own
# directory (mesa_shader_cache_sf), so the first boot fills it once.
# shortcut: the single-file cache has no size limit (~a few MB per driver build); clear
# /b2l-dump/mesa-cache/mesa_shader_cache_sf if the partition runs short.
q = sys.argv[1] + '/air_metal.c'
u = open(q).read()
if 'B1L-0290' not in u:
    a = '''   setenv("MESA_SHADER_CACHE_MAX_SIZE", "1G", 0);
'''
    if u.count(a) != 1:
        sys.exit('air-b2l: the shader cache settings are not as expected (B1L-0290)')
    u = u.replace(a, a + '''   setenv("MESA_DISK_CACHE_SINGLE_FILE", "1", 0);   /* B2L (B1L-0290): index in memory, no write per read */
''')
    open(q, 'w').write(u)
print('air-b2l: Mesa shader cache in a single file (B1L-0290)')

# B1L-0308 (P17): Metal constant buffers at a 16-byte aligned offset are bound by GPU address on panfrost too. Since
# B1L-0062 every one went as a user_buffer, and panfrost_map_constant_buffer_gpu copies the whole bound range into the
# batch pool at each draw: 0307's profile has that memcpy as backboardd's top user function, up to 618 MB per 5 s
# (B2L UBO). 0062's "zeros read by address" was the viewport transform (found in B1L-0065), not the binding.
# panfrost reads a resource UBO by its address (pan_cmdstream.c panfrost_map_constant_buffer_gpu, the path GLES takes
# for every UBO above 0) after panfrost_batch_read_rsrc; its descriptor holds at most 4096 entries of 16 bytes and
# the pointer is shifted by 4 (genxml v7 "Uniform Buffer"), hence constant_buffer_offset_alignment 16.
# Words panfrost pushes into the shader's uniforms (B1L-0063 pushes every UBO) are still read on the CPU, from
# user_buffer + buffer_offset, so user_buffer is the buffer's mapping and panfrost's assert takes a resource with it.
p = sys.argv[1] + '/air_metal.c'
u = open(p).read()
if 'B1L-0308' not in u:
    a = """         if (boffset % device->ubo_offset_align == 0 && !device->ubo_base) {
            cb.buffer = b->res;
         } else {"""
    b = """         if (boffset % device->ubo_offset_align == 0) {
            /* B2L (B1L-0308): by address; user_buffer is the mapping panfrost pushes words from */
            size_t want = r->size && r->size <= left ? r->size : left;
            if (r->b2l_read_end && r->b2l_read_end < want)
               want = r->b2l_read_end;
            cb.buffer = b->res;
            cb.user_buffer = b->map;
            cb.buffer_size = (unsigned)want;
            b2l_ubo_stat(0, 0, true);
         } else {"""
    if u.count(a) != 1:
        sys.exit('air-b2l: the constant buffer binding is not as expected (B1L-0308)')
    u = u.replace(a, b)
    open(p, 'w').write(u)
q = sys.argv[1] + '/../../gallium/drivers/panfrost/pan_cmdstream.c'
u = open(q).read()
if 'B1L-0308' not in u:
    a = """         assert(!cb->buffer && cb->user_buffer &&
                "only user buffers use this path");"""
    b = """         /* B2L (B1L-0308): a Metal buffer is a resource with its CPU mapping as user_buffer */
         assert(cb->user_buffer && "only user buffers use this path");"""
    if u.count(a) != 1:
        sys.exit('air-b2l: pan_cmdstream.c push path is not as expected (B1L-0308)')
    u = u.replace(a, b)
    open(q, 'w').write(u)
print('air-b2l: aligned Metal constant buffers bound by GPU address (B1L-0308)')

# B1L-0329: tile memory. Metal says an attachment's contents need not outlive the pass when its texture is memoryless
# or its store action is DontCare; on Apple's GPUs those stay in tile memory and never reach RAM. Mali renders in
# tiles too, and panfrost already skips writing a colour or depth/stencil buffer back when it is invalidated while the
# batch that draws it is the current one (panfrost_invalidate_resource clears its bit in batch->resolve, so
# pan_job.c sets rts[i].discard -- the glInvalidateFramebuffer path). rwa_texture_discard asks for that, and only when
# the texture is an attachment of the framebuffer the device holds, i.e. of the pass just recorded.
p = sys.argv[1] + '/air_metal.c'
u = open(p).read()
if 'B1L-0329' not in u:
    a = """/* B2L (B1L-0105) */
void *rwa_flush_fence(struct rwa_device *device);"""
    b = """/* B2L (B1L-0329): the pass just drawn need not write `texture` back (memoryless or MTLStoreActionDontCare). 1 when
 * it was an attachment of the current framebuffer and panfrost was told, 0 when it was not one. */
int rwa_texture_discard(struct rwa_texture *texture);
int
rwa_texture_discard(struct rwa_texture *texture)
{
   if (!texture || !texture->res || !texture->device || !texture->device->b2l_fb.width ||
       !texture->device->pipe->invalidate_resource)
      return 0;
   struct rwa_device *device = texture->device;
   bool attached = device->b2l_fb.zsbuf && device->b2l_fb.zsbuf->texture == texture->res;
   for (unsigned i = 0; i < device->b2l_fb.nr_cbufs; i++)
      if (device->b2l_fb.cbufs[i] && device->b2l_fb.cbufs[i]->texture == texture->res)
         attached = true;
   if (!attached)
      return 0;
   device->pipe->invalidate_resource(device->pipe, texture->res);
   return 1;
}

""" + a
    if u.count(a) != 1:
        sys.exit('air-b2l: rwa_flush_fence is not where expected (B1L-0329)')
    u = u.replace(a, b)
    open(p, 'w').write(u)
print('air-b2l: rwa_texture_discard, attachments not written back after their pass (B1L-0329)')

# B1L-0335: in panfrost's last BO cache bucket (4 MB and more, every size mixed) a busy buffer is skipped instead of ending
# the search, and a buffer more than twice the size asked is not taken. Mesa's rule "the oldest is busy, so are the newer"
# only holds inside one size; 0333: backboardd made 114 buffers of 4 MB+ (1937 MB) every 10 s, 946 ms of allocation.
q = sys.argv[1] + '/../../gallium/drivers/panfrost/pan_bo.c'
u = open(q).read()
if 'B1L-0335' not in u:
    a = """      if (panfrost_bo_size(entry) < size || entry->flags != flags)
         continue;

      /* If the oldest BO in the cache is busy, likely so is
       * everything newer, so bail. */
      if (!panfrost_bo_wait(entry, dontwait ? 0 : INT64_MAX, true))
         break;
"""
    b = """      if (panfrost_bo_size(entry) < size || entry->flags != flags)
         continue;
      /* B2L (B1L-0335): the last bucket mixes sizes: not more than twice the size asked */
      if (size >= (1u << MAX_BO_CACHE_BUCKET) && panfrost_bo_size(entry) > 2 * size)
         continue;

      /* If the oldest BO in the cache is busy, likely so is
       * everything newer, so bail. */
      if (!panfrost_bo_wait(entry, dontwait ? 0 : INT64_MAX, true)) {
         if (size >= (1u << MAX_BO_CACHE_BUCKET))
            continue;   /* B2L (B1L-0335): another size may be idle */
         break;
      }
"""
    if u.count(a) != 1:
        sys.exit('air-b2l: pan_bo.c fetch is not as expected (B1L-0335)')
    u = u.replace(a, b)
    open(q, 'w').write(u)
print('air-b2l: big BO cache bucket skips busy buffers')
