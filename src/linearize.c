#include <osc/cfg.h>
#include <osc/debug.h>
#include <osc/arena.h>
#include <string.h>

struct cfg_operand cfg_op_temp(unsigned int temp_id, struct ast_type *type)
{
    struct cfg_operand op = { 0 };

    op.kind = CFG_OPR_TEMP;
    op.temp_id = temp_id;
    op.type = type;

    return op;
}

struct cfg_operand cfg_op_var(struct ast_decl *decl)
{
    struct cfg_operand op = { 0 };

    op.kind = CFG_OPR_VAR;
    op.var_decl = decl;
    if (decl)
        op.type = decl->type;

    return op;
}

struct cfg_operand cfg_op_const(long val, struct ast_type *type)
{
    struct cfg_operand op = { 0 };

    op.kind = CFG_OPR_CONST;
    op.const_val = val;
    op.type = type;

    return op;
}

struct cfg_operand cfg_op_sym(struct symbol *sym)
{
    struct cfg_operand op = { 0 };

    op.kind = CFG_OPR_SYM;
    op.sym = sym;

    return op;
}

struct cfg_function *cfg_function_new(struct osc_context *ctx,
                                      struct ast_decl *decl)
{
    struct cfg_function *fn = NULL;

    BUG_ON(!ctx, "null osc_context");

    fn = osc_arena_zalloc(&ctx->arena, sizeof(*fn));
    BUG_ON(!fn, "arena zalloc failed for cfg_function");

    fn->ctx = ctx;
    fn->decl = decl;
    if (decl)
        fn->name = decl->id;

    list_init(&fn->bbs);
    list_init(&fn->node);

    /* Allocate standard entry and exit blocks */
    fn->entry_bb = cfg_bb_new(fn, "entry");
    fn->exit_bb = cfg_bb_new(fn, "exit");

    return fn;
}

struct basic_block *cfg_bb_new(struct cfg_function *fn, const char *label)
{
    struct basic_block *bb = NULL;

    BUG_ON(!fn, "null cfg_function");

    bb = osc_arena_zalloc(&fn->ctx->arena, sizeof(*bb));
    BUG_ON(!bb, "arena zalloc failed for basic_block");

    bb->id = fn->next_bb_id++;
    bb->label = label;
    bb->func = fn;

    list_init(&bb->instructions);
    list_init(&bb->preds);
    list_init(&bb->succs);
    list_init(&bb->node);

    list_add_tail(&bb->node, &fn->bbs);

    return bb;
}

struct instruction *cfg_insn_new(struct cfg_function *fn,
                                 enum cfg_opcode opcode, struct osc_span span)
{
    struct instruction *insn = NULL;

    BUG_ON(!fn, "null cfg_function");

    insn = osc_arena_zalloc(&fn->ctx->arena, sizeof(*insn));
    BUG_ON(!insn, "arena zalloc failed for instruction");

    insn->opcode = opcode;
    insn->span = span;
    list_init(&insn->args);
    list_init(&insn->node);

    return insn;
}

void cfg_bb_add_insn(struct basic_block *bb, struct instruction *insn)
{
    BUG_ON(!bb, "null basic_block");
    BUG_ON(!insn, "null instruction");

    insn->bb = bb;
    list_add_tail(&insn->node, &bb->instructions);
}

void cfg_insn_add_arg(struct cfg_function *fn, struct instruction *insn,
                      struct cfg_operand op)
{
    struct cfg_arg_node *arg_node = NULL;

    BUG_ON(!fn, "null cfg_function");
    BUG_ON(!insn, "null instruction");

    arg_node = osc_arena_zalloc(&fn->ctx->arena, sizeof(*arg_node));
    BUG_ON(!arg_node, "arena zalloc failed for cfg_arg_node");

    arg_node->op = op;
    list_init(&arg_node->node);
    list_add_tail(&arg_node->node, &insn->args);
    insn->arg_count++;
}

struct cfg_edge *cfg_add_edge(struct cfg_function *fn, struct basic_block *src,
                              struct basic_block *dst)
{
    struct cfg_edge *edge = NULL;

    BUG_ON(!fn, "null cfg_function");
    BUG_ON(!src, "null src basic_block");
    BUG_ON(!dst, "null dst basic_block");

    edge = osc_arena_zalloc(&fn->ctx->arena, sizeof(*edge));
    BUG_ON(!edge, "arena zalloc failed for cfg_edge");

    edge->src = src;
    edge->dst = dst;

    /* A simple backedge check: dst->id <= src->id */
    if (dst->id <= src->id)
        edge->is_backedge = true;

    list_init(&edge->pred_node);
    list_init(&edge->succ_node);

    list_add_tail(&edge->succ_node, &src->succs);
    list_add_tail(&edge->pred_node, &dst->preds);

    return edge;
}
