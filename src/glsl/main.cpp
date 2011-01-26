/*
 * Copyright © 2008, 2009 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
#include <cstdlib>
#include <cstdio>
#include <getopt.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "ast.h"
#include "glsl_parser_extras.h"
#include "glsl_parser.h"
#include "ir_optimization.h"
#include "ir_print_visitor.h"
#include "program.h"
#include "loop_analysis.h"

#include "ir_to_llvm.h"

extern "C" struct gl_shader *
_mesa_new_shader(struct gl_context *ctx, GLuint name, GLenum type);

extern "C" void
_mesa_reference_shader(struct gl_context *ctx, struct gl_shader **ptr,
                       struct gl_shader *sh);

/* Copied from shader_api.c for the stand-alone compiler.
 */
void
_mesa_reference_shader(struct gl_context *ctx, struct gl_shader **ptr,
                       struct gl_shader *sh)
{
   *ptr = sh;
}

struct gl_shader *
_mesa_new_shader(struct gl_context *ctx, GLuint name, GLenum type)
{
   struct gl_shader *shader;

   (void) ctx;

   assert(type == GL_FRAGMENT_SHADER || type == GL_VERTEX_SHADER);
   shader = hieralloc_zero(NULL, struct gl_shader);
   if (shader) {
      shader->Type = type;
      shader->Name = name;
      shader->RefCount = 1;
   }
   return shader;
}

static void
initialize_context(struct gl_context *ctx, gl_api api)
{
   memset(ctx, 0, sizeof(*ctx));

   ctx->API = api;

   ctx->Extensions.ARB_draw_buffers = GL_TRUE;
   ctx->Extensions.ARB_fragment_coord_conventions = GL_TRUE;
   ctx->Extensions.EXT_texture_array = GL_TRUE;
   ctx->Extensions.NV_texture_rectangle = GL_TRUE;

   /* 1.10 minimums. */
   ctx->Const.MaxLights = 8;
   ctx->Const.MaxClipPlanes = 8;
   ctx->Const.MaxTextureUnits = 2;

   /* More than the 1.10 minimum to appease parser tests taken from
    * apps that (hopefully) already checked the number of coords.
    */
   ctx->Const.MaxTextureCoordUnits = 4;

   ctx->Const.VertexProgram.MaxAttribs = 16;
   ctx->Const.VertexProgram.MaxUniformComponents = 512;
   ctx->Const.MaxVarying = 8;
   ctx->Const.MaxVertexTextureImageUnits = 0;
   ctx->Const.MaxCombinedTextureImageUnits = 2;
   ctx->Const.MaxTextureImageUnits = 2;
   ctx->Const.FragmentProgram.MaxUniformComponents = 64;

   ctx->Const.MaxDrawBuffers = 2;

   ctx->Driver.NewShader = _mesa_new_shader;
}

/* Returned string will have 'ctx' as its hieralloc owner. */
static char *
load_text_file(void *ctx, const char *file_name)
{
	char *text = NULL;
	struct stat st;
	ssize_t total_read = 0;
	int fd = open(file_name, O_RDONLY);

	if (fd < 0) {
		return NULL;
	}

	if (fstat(fd, & st) == 0) {
	   text = (char *) hieralloc_size(ctx, st.st_size + 1);
		if (text != NULL) {
			do {
				ssize_t bytes = read(fd, text + total_read,
						     st.st_size - total_read);
				if (bytes < 0) {
					free(text);
					text = NULL;
					break;
				}

				if (bytes == 0) {
					break;
				}

				total_read += bytes;
			} while (total_read < st.st_size);

			text[total_read] = '\0';
		}
	}

	close(fd);

	return text;
}

int glsl_es = 0;
int dump_ast = 0;
int dump_hir = 0;
int dump_lir = 0;
int do_link = 0;
int do_jit = 0;

const struct option compiler_opts[] = {
   { "glsl-es",  0, &glsl_es,  1 },
   { "dump-ast", 0, &dump_ast, 1 },
   { "dump-hir", 0, &dump_hir, 1 },
   { "dump-lir", 0, &dump_lir, 1 },
   { "link",     0, &do_link,  1 },
   { "do-jit",   0, &do_jit,   1 },
   { NULL, 0, NULL, 0 }
};

/**
 * \brief Print proper usage and exit with failure.
 */
void
usage_fail(const char *name)
{

   const char *header =
      "usage: %s [options] <file.vert | file.geom | file.frag>\n"
      "\n"
      "Possible options are:\n";
   printf(header, name, name);
   for (const struct option *o = compiler_opts; o->name != 0; ++o) {
      printf("    --%s\n", o->name);
   }
   exit(EXIT_FAILURE);
}


void
compile_shader(struct gl_context *ctx, struct gl_shader *shader)
{
   struct _mesa_glsl_parse_state *state =
      new(shader) _mesa_glsl_parse_state(ctx, shader->Type, shader);

   const char *source = shader->Source;
   state->error = preprocess(state, &source, &state->info_log,
			     state->extensions, ctx->API);

   if (!state->error) {
      _mesa_glsl_lexer_ctor(state, source);
      _mesa_glsl_parse(state);
      _mesa_glsl_lexer_dtor(state);
   }

   if (dump_ast) {
      foreach_list_const(n, &state->translation_unit) {
	 ast_node *ast = exec_node_data(ast_node, n, link);
	 ast->print();
      }
      printf("\n\n");
   }

   shader->ir = new(shader) exec_list;
   if (!state->error && !state->translation_unit.is_empty())
      _mesa_ast_to_hir(shader->ir, state);

   /* Print out the unoptimized IR. */
   if (!state->error && dump_hir) {
      validate_ir_tree(shader->ir);
      _mesa_print_ir(shader->ir, state);
   }

   /* Optimization passes */
   if (!state->error && !shader->ir->is_empty()) {
      bool progress;
      do {
	 progress = do_common_optimization(shader->ir, false, 32);
      } while (progress);

      validate_ir_tree(shader->ir);
   }


   /* Print out the resulting IR */
   if (!state->error && dump_lir) {
      _mesa_print_ir(shader->ir, state);
   }

   shader->symbols = state->symbols;
   shader->CompileStatus = !state->error;
   shader->Version = state->language_version;
   memcpy(shader->builtins_to_link, state->builtins_to_link,
	  sizeof(shader->builtins_to_link[0]) * state->num_builtins_to_link);
   shader->num_builtins_to_link = state->num_builtins_to_link;

   if (shader->InfoLog)
      hieralloc_free(shader->InfoLog);

   shader->InfoLog = state->info_log;

   /* Retain any live IR, but trash the rest. */
   reparent_ir(shader->ir, shader);

   hieralloc_free(state);

   return;
}

//#define DRAW_TO_SCREEN 1
#include "image_file.h"

#if defined __arm__ && defined DRAW_TO_SCREEN
extern "C" int SetupDrawingSurface(unsigned * width, unsigned * height, unsigned * bpp);
extern "C" void * PresentDrawingSurface();
extern "C" void DisposeDrawingSurface();
#endif 

void execute(void (* function)(), float * data)
{
#if defined __arm__ && DRAW_TO_SCREEN
   unsigned width = 0, height = 0, bpp = 0;
   int err = SetupDrawingSurface(&width, &height, &bpp);
   assert(!err);
   assert(32 == bpp);
   unsigned * frameSurface = (unsigned *)PresentDrawingSurface();
#else
   const unsigned width = 480, height = 800;
   unsigned * frameSurface = new unsigned [width * height];
#endif
   const unsigned scale = 16, portWidth = 80, portHeight = 50;
   float * constants = data + 36;
   float * outputs = data + 0;
   float * inputs = data + 12;
   printf("executing... \n function=%p, data=%p \n", function, data);

   /*
   #ifdef __arm__
   {
      volatile unsigned wait = 1;
      printf("waiting for attach, set wait(%p) to 0 \n", &wait);
      puts("");
      while (wait);
   }
   #endif
   //*/
   


   unsigned frames = 1;
   clock_t c0 = clock();
   
   while(true)
   for (frames = 1; frames <= 100; frames++)
   {
      inputs[2] = 0;
      inputs[3] = 1;
      constants[0] = frames * 0.6f;
            
      //for (unsigned i = 0; i < 480 * 800; i++)
      for (unsigned y = 0; y < portHeight; y++)
         for (unsigned x = 0; x < portWidth; x++) {
            //data[36] = (float)i / 10000;
            //memset(data, i, sizeof(data));
            inputs[0] = ((float)x) / (portWidth - 1);
            inputs[1] = ((float)y) / (portHeight - 1);
            function();
            unsigned r = outputs[0] * 255;
            unsigned g = outputs[1] * 255;
            unsigned b = outputs[2] * 255;
            unsigned a = outputs[3] * 255;
            frameSurface[y * width + x] = (a << 24) | (b << 16) | (g << 8) | r;
         }
      //*
      for (int y = portHeight - 1; y >= 0; y--)
         for (int x = portWidth - 1; x >= 0; x--)
         {
               unsigned pixel = ((unsigned *)frameSurface)[y * width + x];
               for (unsigned xx = 0; xx < scale; xx++)
                  for (unsigned yy = 0; yy < scale; yy++)
                     ((unsigned *)frameSurface)[(y * scale + yy) * width + x * scale + xx] = pixel;
         }
         //*/
#if defined __arm__ && DRAW_TO_SCREEN
      frameSurface = (unsigned *)PresentDrawingSurface();
#endif
   }

   float elapsed = (float)(clock() - c0) / CLOCKS_PER_SEC;
   printf ("\n *** test_scan elapsed CPU time: %fs \n *** fps=%.2f, tpf=%.2fms \n",
           elapsed, frames / elapsed, elapsed / frames * 1000);
   printf("gl_FragColor=%.2f, %.2f, %.2f %.2f \n", outputs[0], outputs[1], outputs[2], outputs[3]);
   assert(0.1f < outputs[3]);
#if defined __arm__
   SaveBMP("/sdcard/mesa.bmp", frameSurface, width, height);
#else
   SaveBMP("mesa.bmp", frameSurface, width, height);
#endif
#if DRAW_TO_SCREEN
   void DisposeDrawingSurface();
#else
   delete frameSurface;
#endif
 
}

//#def USE_LLVM_EXECUTIONENGINE 1
#if USE_LLVM_EXECUTIONENGINE

#include <llvm/ExecutionEngine/JIT.h>
#include <llvm/Target/TargetSelect.h>

void jit(llvm::Module * mod)
{
#ifndef __arm__
   __attribute__ ((aligned (16))) // LLVM generates movaps on X86, needs 16 bytes align
#endif
   float data [64];
   memset(data, 0xff, sizeof(data));

   llvm::InitializeNativeTarget();

   std::string errorString;
   llvm::EngineBuilder engineBuilder(mod);
   engineBuilder.setEngineKind(llvm::EngineKind::JIT);
   engineBuilder.setErrorStr(&errorString);
#ifdef __arm__
   engineBuilder.setMAttrs(llvm::SmallVector<std::string, 1>(1,"vfp3"));
   mod->setTargetTriple("armv7-none-linux-gnueabi");
#endif

   llvm::ExecutionEngine * ee = engineBuilder.create();
   if (!ee)
      puts(errorString.c_str());
   assert(ee);

   ee->DisableLazyCompilation();

   if ((mod->getFunction("putchar")))
      ee->updateGlobalMapping(mod->getFunction("putchar"), (void *)putchar);
   if ((mod->getFunction("sinf")))
      ee->updateGlobalMapping(mod->getFunction("sinf"), (void *)sinf);
   if ((mod->getFunction("cosf")))
      ee->updateGlobalMapping(mod->getFunction("cosf"), (void *)cosf);
   if ((mod->getFunction("powf")))
      ee->updateGlobalMapping(mod->getFunction("powf"), (void *)cosf);

   ee->updateGlobalMapping(mod->getGlobalVariable("gl_FragColor"), (void *)(data + 0));
   ee->updateGlobalMapping(mod->getGlobalVariable("gl_FragCoord"), (void *)(data + 4));
   ee->updateGlobalMapping(mod->getGlobalVariable("gl_FrontFacing"), (void *)(data + 8));
   ee->updateGlobalMapping(mod->getGlobalVariable("vTexCoord"), (void *)(data + 12));
   ee->updateGlobalMapping(mod->getGlobalVariable("t"), (void *)(data + 36));

   llvm::Function * func = mod->getFunction("main");
   assert(func);

   void (* function)() = (void (*)())ee->getPointerToFunction(func);
   execute(function, data);
   puts("USE_LLVM_EXECUTIONENGINE");
}

#else

#include <bcc/bcc.h>
#include <dlfcn.h>

static void* symbolLookup(void* pContext, const char* name)
{
   float * data = (float *)pContext;
   void * symbol = (void*)dlsym(RTLD_DEFAULT, name);
   if (NULL == symbol) {
      if (0 == strcmp("gl_FragColor", name))
         symbol = data + 0;
      else if (0 == strcmp("gl_FragCoord", name))
         symbol = data + 4;
      else if (0 == strcmp("gl_FrontFacing", name))
         symbol = data + 8;
      else if (0 == strcmp("vTexCoord", name)) {
         symbol = data + 12;
         *(data + 12) = 1.1;
         *(data + 13) = 1.2;
         *(data + 14) = 1.3;
         *(data + 15) = 1;
      } else if (0 == strcmp("uRotM", name)) {
         symbol = data + 16;
         memset(data + 16, 0, 16 * sizeof(*data));
         data[16] = data[21] = data[26] = data[31] = 1;
         data[28] = 11;
         data[29] = 22;
         data[30] = 33;
         //data[31] = 44;
      } else if (0 == strcmp("uFragmentColor", name)) {
         symbol = data + 32;
         data[32] = 1.57075f;
         data[33] = 1.57075f;
         data[34] = 1.57075f;
         data[35] = 1.57075f;
      } else if (0 == strcmp("t", name)) {
         symbol = data + 36;
         data[36] = 0.1f;
      }

   }
   printf("symbolLookup '%s'=%p \n", name, symbol);
   //getchar();
   assert(symbol);
   return symbol;
}

void jit(llvm::Module * mod)
{
#ifndef __arm__
   __attribute__ ((aligned (16))) // LLVM generates movaps on X86, needs 16 bytes align
#endif
   float data [64];
   memset(data, 0xff, sizeof(data));

   BCCScriptRef script = bccCreateScript();
   bccReadModule(script, "glsl", (LLVMModuleRef)mod, 0);
   int result = 0;
   assert(0 == bccGetError(script));
   bccRegisterSymbolCallback(script, symbolLookup, data);
   assert(0 == bccGetError(script));
   bccPrepareExecutable(script, NULL, 0);
   result = bccGetError(script);
   if (result != 0) {
      puts("failed bcc_compile");
      assert(0);
      return;
   }

   void (* function)() = (void (*)())bccGetFuncAddr(script, "main");
   result = bccGetError(script);
   if (result != BCC_NO_ERROR)
      fprintf(stderr, "Could not find '%s': %d\n", "main", result);
   else
      printf("bcc_compile %s=%p \n", "main", function);
   execute(function, data);
}

#endif

struct _mesa_glsl_parse_state * global_state = NULL;

int
main(int argc, char **argv)
{
   //*
   if (1 == argc) {
      argc = 6;
      char shader_file_path[256] = {0};
      memcpy(shader_file_path, argv[0], strlen(argv[0]));
      char * slash = shader_file_path + strlen(shader_file_path);
      while (*slash != '/')
         slash--;
      const char shader_file[] = "stress_fs.frag";
      memcpy(slash + 1, shader_file, strlen(shader_file));
      const char * args [] = {argv[0], "--dump-hir", "--do-jit", "--link", "--glsl-es", shader_file_path};
      argv = (char **)args;
   }
   //*/
   int status = EXIT_SUCCESS;
   struct gl_context local_ctx;
   struct gl_context *ctx = &local_ctx;

   int c;
   int idx = 0;
   while ((c = getopt_long(argc, argv, "", compiler_opts, &idx)) != -1)
      /* empty */ ;


   if (argc <= optind)
      usage_fail(argv[0]);

   initialize_context(ctx, (glsl_es) ? API_OPENGLES2 : API_OPENGL);

   struct gl_shader_program *whole_program;

   whole_program = hieralloc_zero (NULL, struct gl_shader_program);
   assert(whole_program != NULL);

   for (/* empty */; argc > optind; optind++) {
      whole_program->Shaders = (struct gl_shader **)
         hieralloc_realloc(whole_program, whole_program->Shaders,
         struct gl_shader *, whole_program->NumShaders + 1);
      assert(whole_program->Shaders != NULL);

      struct gl_shader *shader = hieralloc_zero(whole_program, gl_shader);

      whole_program->Shaders[whole_program->NumShaders] = shader;
      whole_program->NumShaders++;

      const unsigned len = strlen(argv[optind]);
      if (len < 6)
	 usage_fail(argv[0]);

      const char *const ext = & argv[optind][len - 5];
      if (strncmp(".vert", ext, 5) == 0)
	 shader->Type = GL_VERTEX_SHADER;
      else if (strncmp(".geom", ext, 5) == 0)
	 shader->Type = GL_GEOMETRY_SHADER;
      else if (strncmp(".frag", ext, 5) == 0)
	 shader->Type = GL_FRAGMENT_SHADER;
      else
	 usage_fail(argv[0]);

      shader->Source = load_text_file(whole_program, argv[optind]);
      if (shader->Source == NULL) {
	 printf("File \"%s\" does not exist.\n", argv[optind]);
	 exit(EXIT_FAILURE);
      }

      compile_shader(ctx, shader);

      if (!shader->CompileStatus) {
	 printf("Info log for %s:\n%s\n", argv[optind], shader->InfoLog);
	 status = EXIT_FAILURE;
	 break;
      }
   }

   if ((status == EXIT_SUCCESS) && do_link)  {
      link_shaders(ctx, whole_program);
      status = (whole_program->LinkStatus) ? EXIT_SUCCESS : EXIT_FAILURE;

      if (strlen(whole_program->InfoLog) > 0)
         printf("Info log for linking:\n%s\n", whole_program->InfoLog);
   }

   puts("jit");

   for (unsigned i = 0; do_jit && i < MESA_SHADER_TYPES; i++) {
      struct gl_shader *shader = whole_program->_LinkedShaders[i];
      if (!shader)
         continue;
      global_state = new(shader) _mesa_glsl_parse_state(ctx, shader->Type, shader);
      exec_list * ir = shader->ir;

      do_mat_op_to_vec(ir);

      puts("\n *** IR for JIT *** \n");
      _mesa_print_ir(ir, global_state);

      llvm::Module * module = glsl_ir_to_llvm_module(ir);
      assert(module);
      puts("\n *** Module for JIT *** \n");
      module->dump();
      jit(module);
      puts("jitted");
   }

   for (unsigned i = 0; i < MESA_SHADER_TYPES; i++)
      hieralloc_free(whole_program->_LinkedShaders[i]);

   hieralloc_free(whole_program);
   _mesa_glsl_release_types();
   _mesa_glsl_release_functions();

   puts("mesa exit");
   hieralloc_report_brief(NULL, stdout);
   return status;
}