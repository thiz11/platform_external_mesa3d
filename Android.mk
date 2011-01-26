USE_LLVM_EXECUTIONENGINE := false
# using libbcc:
# need to remove FORCE_ARM_CODEGEN and add some libs in libbcc/Android.mk

# if using libLLVMExecutionEngine, 
# need to #define USE_LLVM_EXECUTIONENGINE 1" in mesa.h and uncomment LOCAL_STATIC_LIBRARIES
# also need to add files to several Android.mk in llvm, and comment out some stuff in llvm DynamicLibrary.cpp and Intercept.cpp

ifneq ($(TARGET_SIMULATOR),true)

LOCAL_PATH := $(call my-dir)
LLVM_ROOT_PATH := external/llvm

# these are for using llvm::ExecutionEngine, also remove libbcc
#libLLVMX86CodeGen;libLLVMX86Info;libLLVMBitReader;libLLVMSelectionDAG;libLLVMAsmPrinter;libLLVMJIT;libLLVMCodeGen;libLLVMTarget;libLLVMMC;libLLVMScalarOpts;libLLVMipo;libLLVMTransformUtils;libLLVMCore;libLLVMSupport;libLLVMSystem;libLLVMAnalysis;libLLVMInstCombine;libLLVMipa;libLLVMMCParser;libLLVMExecutionEngine;
mesa_STATIC_LIBS :=	\
	libLLVMBitReader	\
	libLLVMSelectionDAG	\
	libLLVMAsmPrinter	\
	libLLVMJIT	\
	libLLVMCodeGen	\
	libLLVMTarget	\
	libLLVMMC	\
	libLLVMScalarOpts	\
	libLLVMipo		\
	libLLVMTransformUtils	\
	libLLVMCore	\
	libLLVMSupport	\
	libLLVMSystem	\
	libLLVMAnalysis \
	libLLVMInstCombine \
	libLLVMipa	\
	libLLVMMCParser	\
	libLLVMExecutionEngine

mesa_SRC_FILES :=	\
	src/glsl/glcpp/pp.c \
	src/glsl/glcpp/glcpp-lex.c \
	src/glsl/glcpp/glcpp-parse.c \
	src/glsl/ast_expr.cpp \
	src/glsl/ast_function.cpp \
	src/glsl/ast_to_hir.cpp \
	src/glsl/ast_type.cpp \
	src/glsl/builtin_function.cpp \
	src/glsl/glsl_lexer.cpp \
	src/glsl/glsl_parser.cpp \
	src/glsl/glsl_parser_extras.cpp \
	src/glsl/glsl_symbol_table.cpp \
	src/glsl/glsl_types.cpp \
	src/glsl/hir_field_selection.cpp \
	src/glsl/ir.cpp \
	src/glsl/ir_basic_block.cpp \
	src/glsl/ir_clone.cpp \
	src/glsl/ir_constant_expression.cpp \
	src/glsl/ir_expression_flattening.cpp \
	src/glsl/ir_function.cpp \
	src/glsl/ir_function_can_inline.cpp \
	src/glsl/ir_hierarchical_visitor.cpp \
	src/glsl/ir_hv_accept.cpp \
	src/glsl/ir_import_prototypes.cpp \
	src/glsl/ir_print_visitor.cpp \
	src/glsl/ir_reader.cpp \
	src/glsl/ir_rvalue_visitor.cpp \
	src/glsl/ir_set_program_inouts.cpp \
	src/glsl/ir_validate.cpp \
	src/glsl/ir_variable.cpp \
	src/glsl/ir_variable_refcount.cpp \
	src/glsl/link_functions.cpp \
	src/glsl/linker.cpp \
	src/glsl/loop_analysis.cpp \
	src/glsl/loop_controls.cpp \
	src/glsl/loop_unroll.cpp \
	src/glsl/lower_discard.cpp \
	src/glsl/lower_if_to_cond_assign.cpp \
	src/glsl/lower_instructions.cpp \
	src/glsl/lower_jumps.cpp \
	src/glsl/lower_mat_op_to_vec.cpp \
	src/glsl/lower_noise.cpp \
	src/glsl/lower_texture_projection.cpp \
	src/glsl/lower_variable_index_to_cond_assign.cpp \
	src/glsl/lower_vec_index_to_cond_assign.cpp \
	src/glsl/lower_vec_index_to_swizzle.cpp \
	src/glsl/lower_vector.cpp \
	src/glsl/main.cpp \
	src/glsl/opt_algebraic.cpp \
	src/glsl/opt_constant_folding.cpp \
	src/glsl/opt_constant_propagation.cpp \
	src/glsl/opt_constant_variable.cpp \
	src/glsl/opt_copy_propagation.cpp \
	src/glsl/opt_dead_code.cpp \
	src/glsl/opt_dead_code_local.cpp \
	src/glsl/opt_dead_functions.cpp \
	src/glsl/opt_discard_simplification.cpp \
	src/glsl/opt_function_inlining.cpp \
	src/glsl/opt_if_simplification.cpp \
	src/glsl/opt_noop_swizzle.cpp \
	src/glsl/opt_redundant_jumps.cpp \
	src/glsl/opt_structure_splitting.cpp \
	src/glsl/opt_swizzle_swizzle.cpp \
	src/glsl/opt_tree_grafting.cpp \
	src/glsl/s_expression.cpp \
	src/glsl/strtod.c \
	src/glsl/ir_to_llvm.cpp \
	src/mesa/program/hash_table.c \
	src/mesa/program/symbol_table.c \
	src/talloc/hieralloc.c
	
# Executable for host
# ========================================================
include $(CLEAR_VARS)

LOCAL_MODULE_TAGS := optional

LOCAL_CPPFLAGS := -DDEBUG -UNDEBUG -O0 -g
LOCAL_CFLAGS := -DDEBUG -UNDEBUG -O0 -g 

LOCAL_MODULE := mesa
LOCAL_SRC_FILES := $(mesa_SRC_FILES)

ifeq ($(USE_LLVM_EXECUTIONENGINE),true)
LOCAL_CPPFLAGS += -DUSE_LLVM_EXECUTIONENGINE
LOCAL_CFLAGS += -DUSE_LLVM_EXECUTIONENGINE
LOCAL_STATIC_LIBRARIES := libLLVMX86CodeGen libLLVMX86Info $(mesa_STATIC_LIBS)
else
LOCAL_SHARED_LIBRARIES := libbcc
endif

LOCAL_LDLIBS := -ldl -lpthread
LOCAL_C_INCLUDES :=	\
	$(LOCAL_PATH)	\
	$(LOCAL_PATH)/src/glsl	\
	$(LOCAL_PATH)/src/mesa	\
	$(LOCAL_PATH)/src/talloc	\
	$(LOCAL_PATH)/src/mapi	\
	$(LOCAL_PATH)/include	\
	$(LOCAL_PATH)/../libbcc/include

include $(LLVM_ROOT_PATH)/llvm-host-build.mk
include $(BUILD_HOST_EXECUTABLE)


# Executable for target
# ========================================================
include $(CLEAR_VARS)

#LOCAL_PRELINK_MODULE := false
LOCAL_MODULE_TAGS := optional

#LOCAL_CPPFLAGS += -fPIC
#LOCAL_CFLAGS += -fPIC

#LOCAL_CPPFLAGS += -mfpu=neon
#LOCAL_CFLAGS += -mfpu=neon
LOCAL_CPPFLAGS += -DDEBUG -UNDEBUG -O0 -g
LOCAL_CFLAGS += -DDEBUG -UNDEBUG -O0 -g 

LOCAL_MODULE := mesa
LOCAL_SRC_FILES := $(mesa_SRC_FILES)

LOCAL_SHARED_LIBRARIES := libstlport libcutils libdl

LOCAL_SRC_FILES += egl.cpp
#libutils libhardware libsurfaceflinger_client libpixelflinger
# libsurfaceflinger_client and libpixelflinger causes hieralloc assertion
LOCAL_SHARED_LIBRARIES += libutils libhardware libsurfaceflinger_client libpixelflinger
LOCAL_CPPFLAGS += -DDRAW_TO_SCREEN=1
LOCAL_CFLAGS += -fvisibility=hidden
LOCAL_CFLAGS += -fstrict-aliasing

ifeq ($(USE_LLVM_EXECUTIONENGINE),true)
LOCAL_CPPFLAGS += -DUSE_LLVM_EXECUTIONENGINE
LOCAL_CFLAGS += -DUSE_LLVM_EXECUTIONENGINE
LOCAL_STATIC_LIBRARIES :=  libLLVMARMCodeGen libLLVMARMInfo libLLVMARMDisassembler libLLVMARMAsmPrinter $(mesa_STATIC_LIBS)
else
LOCAL_SHARED_LIBRARIES += libbcc 
endif

LOCAL_C_INCLUDES :=	\
	$(LOCAL_PATH)	\
	$(LOCAL_PATH)/src/glsl	\
	$(LOCAL_PATH)/src/mesa	\
	$(LOCAL_PATH)/src/talloc	\
	$(LOCAL_PATH)/src/mapi	\
	$(LOCAL_PATH)/include	\
	$(LOCAL_PATH)/../libbcc/include

include $(LLVM_ROOT_PATH)/llvm-device-build.mk
include $(BUILD_EXECUTABLE)

endif # TARGET_SIMULATOR != true