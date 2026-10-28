#ifndef __OSC_CFG_H__
#define __OSC_CFG_H__

#include <osc/compiler.h>
#include <osc/list.h>
#include <osc/context.h>
#include <osc/ast.h>
#include <stdbool.h>

/* CFG 3-Address Code Instruction Opcodes */
enum cfg_opcode {
    OP_NOP = 0,
    OP_ENTRY, /* Function entry point */
    OP_ALLOC, /* Stack variable allocation */
    OP_LOAD, /* Load from variable or memory: target = *src1 or target = src1 */
    OP_STORE, /* Store to variable or memory: *target = src1 or target = src1 */
    OP_CALL, /* Function call: target = callee(args...) */
    OP_CLEANUP, /* RAII cleanup invocation: cleanup_fn(&var) */
    OP_SCOPE_END, /* Lexical scope boundary: variable exits scope */
    OP_RET, /* Return from function: return src1 */
    OP_BR, /* Unconditional branch to target_bb */
    OP_CBR, /* Conditional branch on src1: true_bb / false_bb */
    OP_SWITCH, /* Multiway switch on src1 */
    OP_ASM, /* Inline assembly statement */
    OP_PHI, /* Dataflow phi node */
};

/* CFG Operand Kinds */
enum cfg_operand_kind {
    CFG_OPR_NONE = 0,
    CFG_OPR_CONST,
    CFG_OPR_VAR,
    CFG_OPR_TEMP,
    CFG_OPR_SYM,
};

/* CFG Operand */
struct cfg_operand {
    enum cfg_operand_kind kind;
    struct ast_type *type;
    union {
        long const_val;
        struct symbol *sym;
        struct ast_decl *var_decl;
        unsigned int temp_id;
    };
};

/* Call Argument Node */
struct cfg_arg_node {
    struct cfg_operand op;
    struct list_head node;
};

struct basic_block;
struct cfg_function;

/* 3-Address Pseudo-Instruction */
struct instruction {
    enum cfg_opcode opcode;
    struct osc_span span;

    struct cfg_operand target;
    struct cfg_operand src1;
    struct cfg_operand src2;

    /* Binary operator symbol if arithmetic/logical (e.g. sym_add, sym_lt) */
    int op_sym;

    /* Call / Cleanup specific metadata */
    struct symbol *callee_sym;
    struct list_head args; /* List of struct cfg_arg_node */
    size_t arg_count;

    /* Branch specific metadata */
    struct basic_block *target_bb;
    struct basic_block *true_bb;
    struct basic_block *false_bb;

    /* Inline assembly */
    const char *asm_code;

    /* Kernel semantic projections */
    bool is_container_of;
    struct symbol *container_member;
    struct ast_type *container_type;
    bool is_err_check;
    bool is_deref;

    struct basic_block *bb; /* Enclosing basic block */
    struct list_head node; /* Link in bb->instructions */
};

/* Directed Control Flow Edge between Basic Blocks */
struct cfg_edge {
    struct basic_block *src;
    struct basic_block *dst;
    bool is_backedge;

    struct list_head pred_node; /* Link in dst->preds */
    struct list_head succ_node; /* Link in src->succs */
};

/* Basic Block */
struct basic_block {
    unsigned int id;
    const char *label;

    struct list_head instructions; /* List of struct instruction */
    struct list_head preds; /* List of struct cfg_edge (where dst == this) */
    struct list_head succs; /* List of struct cfg_edge (where src == this) */

    struct cfg_function *func; /* Enclosing function */
    struct list_head node; /* Link in func->bbs */

    /* Dataflow lattice state hook for pluggable checkers */
    void *df_in;
    void *df_out;
};

/* Linearized CFG Function */
struct cfg_function {
    struct symbol *name;
    struct ast_decl *decl; /* Original AST function declaration */

    struct basic_block *entry_bb;
    struct basic_block *exit_bb;
    struct list_head bbs; /* List of struct basic_block */

    unsigned int next_bb_id;
    unsigned int next_temp_id;

    struct osc_context *ctx;
    struct list_head node; /* Link in translation unit CFG function list */
};

/* CFG Constructor and Management Functions */
struct cfg_function *cfg_function_new(struct osc_context *ctx,
                                      struct ast_decl *decl);
struct basic_block *cfg_bb_new(struct cfg_function *fn, const char *label);
struct instruction *cfg_insn_new(struct cfg_function *fn,
                                 enum cfg_opcode opcode, struct osc_span span);
void cfg_bb_add_insn(struct basic_block *bb, struct instruction *insn);
struct cfg_edge *cfg_add_edge(struct cfg_function *fn, struct basic_block *src,
                              struct basic_block *dst);

/* Operand Constructors */
struct cfg_operand cfg_op_temp(unsigned int temp_id, struct ast_type *type);
struct cfg_operand cfg_op_var(struct ast_decl *decl);
struct cfg_operand cfg_op_const(long val, struct ast_type *type);
struct cfg_operand cfg_op_sym(struct symbol *sym);

/* Instruction Helpers */
void cfg_insn_add_arg(struct cfg_function *fn, struct instruction *insn,
                      struct cfg_operand op);

/* Translation Unit Linearization */
struct cfg_function *linearize_function(struct osc_context *ctx,
                                        struct ast_decl *fn_decl);
struct list_head *linearize_translation_unit(struct osc_context *ctx,
                                             struct ast_translation_unit *tu,
                                             struct list_head *out_cfg_funcs);

/* CFG Pretty Printer and Visualizers */
void cfg_dump_text(struct cfg_function *fn, FILE *out);
void cfg_dump_dot(struct cfg_function *fn, FILE *out);

#endif /* __OSC_CFG_H__ */
