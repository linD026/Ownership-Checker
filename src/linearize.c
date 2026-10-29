#include <osc/cfg.h>
#include <osc/parser.h>
#include <osc/token.h>
#include <osc/debug.h>
#include <osc/arena.h>
#include <string.h>

/**
 * cfg_op_temp - Construct a temporary pseudo-register operand
 * @temp_id: Unique virtual temporary register index
 * @type: Semantic AST type descriptor
 *
 * Return: Initialized struct cfg_operand of kind CFG_OPR_TEMP.
 */
struct cfg_operand cfg_op_temp(unsigned int temp_id, struct ast_type *type)
{
    struct cfg_operand op = { 0 };

    op.kind = CFG_OPR_TEMP;
    op.temp_id = temp_id;
    op.type = type;

    return op;
}

/**
 * cfg_op_var - Construct a variable reference operand
 * @decl: Target variable AST declaration node
 *
 * Return: Initialized struct cfg_operand of kind CFG_OPR_VAR.
 */
struct cfg_operand cfg_op_var(struct ast_decl *decl)
{
    struct cfg_operand op = { 0 };

    op.kind = CFG_OPR_VAR;
    op.var_decl = decl;
    if (decl)
        op.type = decl->type;

    return op;
}

/**
 * cfg_op_const - Construct a constant scalar literal operand
 * @val: Integer constant value
 * @type: Semantic AST type descriptor
 *
 * Return: Initialized struct cfg_operand of kind CFG_OPR_CONST.
 */
struct cfg_operand cfg_op_const(long val, struct ast_type *type)
{
    struct cfg_operand op = { 0 };

    op.kind = CFG_OPR_CONST;
    op.const_val = val;
    op.type = type;

    return op;
}

/**
 * cfg_op_sym - Construct a symbolic operand reference
 * @sym: Identifier symbol pointer
 *
 * Return: Initialized struct cfg_operand of kind CFG_OPR_SYM.
 */
struct cfg_operand cfg_op_sym(struct symbol *sym)
{
    struct cfg_operand op = { 0 };

    op.kind = CFG_OPR_SYM;
    op.sym = sym;

    return op;
}

/**
 * cfg_function_new - Allocate and initialize a CFG function graph
 * @ctx: Active per-file compilation context
 * @decl: Function definition AST declaration node
 *
 * Allocates entry and exit basic blocks and binds the graph to the
 * context arena.
 *
 * Return: Pointer to initialized struct cfg_function.
 */
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

/**
 * cfg_bb_new - Allocate and append a new basic block to a CFG function
 * @fn: Enclosing CFG function graph
 * @label: Human-readable diagnostic label
 *
 * Return: Pointer to initialized struct basic_block.
 */
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

/**
 * cfg_insn_new - Allocate an unlinked 3-address pseudo-instruction
 * @fn: Enclosing CFG function graph
 * @opcode: Instruction opcode
 * @span: Source code coordinates span
 *
 * Return: Pointer to initialized struct instruction.
 */
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

/**
 * cfg_bb_add_insn - Append an instruction to the end of a basic block
 * @bb: Target basic block
 * @insn: Instruction to append
 */
void cfg_bb_add_insn(struct basic_block *bb, struct instruction *insn)
{
    BUG_ON(!bb, "null basic_block");
    BUG_ON(!insn, "null instruction");

    insn->bb = bb;
    list_add_tail(&insn->node, &bb->instructions);
}

/**
 * cfg_insn_add_arg_proj - Append a call argument operand with projection metadata
 * @fn: Enclosing CFG function graph
 * @insn: Call or cleanup instruction
 * @op: Argument operand
 * @proj_var: Root container variable if projection &ptr->member
 * @proj_member: Intrusive link member symbol
 */
void cfg_insn_add_arg_proj(struct cfg_function *fn, struct instruction *insn,
                           struct cfg_operand op, struct ast_decl *proj_var,
                           struct symbol *proj_member)
{
    struct cfg_arg_node *arg_node = NULL;

    BUG_ON(!fn, "null cfg_function");
    BUG_ON(!insn, "null instruction");

    arg_node = osc_arena_zalloc(&fn->ctx->arena, sizeof(*arg_node));
    BUG_ON(!arg_node, "arena zalloc failed for cfg_arg_node");

    arg_node->op = op;
    arg_node->proj_var = proj_var;
    arg_node->proj_member = proj_member;
    list_init(&arg_node->node);
    list_add_tail(&arg_node->node, &insn->args);
    insn->arg_count++;
}

/**
 * cfg_insn_add_arg - Append a call argument operand to an instruction
 * @fn: Enclosing CFG function graph
 * @insn: Call or cleanup instruction
 * @op: Argument operand
 */
void cfg_insn_add_arg(struct cfg_function *fn, struct instruction *insn,
                      struct cfg_operand op)
{
    cfg_insn_add_arg_proj(fn, insn, op, NULL, NULL);
}

/**
 * cfg_add_edge - Create a directed control-flow edge between basic blocks
 * @fn: Enclosing CFG function graph
 * @src: Source basic block
 * @dst: Destination basic block
 *
 * Inserts edge into src's successor list and dst's predecessor list,
 * automatically flagging backedges when dst->id <= src->id.
 *
 * Return: Pointer to initialized struct cfg_edge.
 */
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

/* Linearization Local Scope and Context Structures */

struct linearize_var {
    struct ast_decl *decl;
    struct symbol *cleanup_fn;
    struct list_head node;
};

struct linearize_scope {
    struct list_head vars; /* List of struct linearize_var in LIFO order */
    struct basic_block *break_bb;
    struct basic_block *continue_bb;
    struct linearize_scope *prev;
};

struct linearize_label {
    struct symbol *name;
    struct basic_block *bb;
    struct linearize_scope *scope;
    struct list_head node;
};

struct linearize_context {
    struct cfg_function *fn;
    struct osc_context *ctx;
    struct basic_block *curr_bb;
    struct linearize_scope *scope;
    struct list_head labels; /* List of struct linearize_label */
};

/**
 * bb_has_terminator - Check whether basic block ends in a branch or return
 * @bb: Basic block to inspect
 *
 * Return: True if last instruction is OP_BR, OP_CBR, or OP_RET.
 */
static bool bb_has_terminator(struct basic_block *bb)
{
    struct instruction *last = NULL;

    if (!bb || list_empty(&bb->instructions))
        return false;

    last = list_last_entry(&bb->instructions, struct instruction, node);
    return (last->opcode == OP_BR || last->opcode == OP_CBR ||
            last->opcode == OP_RET);
}

/**
 * linearize_find_or_create_label - Lookup or instantiate basic block for label
 * @lctx: Active linearization context
 * @name: Label identifier symbol
 *
 * Return: Basic block corresponding to label target.
 */
static struct basic_block *
linearize_find_or_create_label(struct linearize_context *lctx,
                               struct symbol *name)
{
    struct linearize_label *l = NULL;
    struct basic_block *bb = NULL;

    if (!name)
        return NULL;

    list_for_each_entry (l, &lctx->labels, node) {
        if (l->name == name || strcmp(l->name->name, name->name) == 0)
            return l->bb;
    }

    bb = cfg_bb_new(lctx->fn, name->name);
    l = osc_arena_zalloc(&lctx->ctx->arena, sizeof(*l));
    BUG_ON(!l, "arena zalloc failed for linearize_label");

    l->name = name;
    l->bb = bb;
    l->scope = lctx->scope;
    list_init(&l->node);
    list_add_tail(&l->node, &lctx->labels);

    return bb;
}

/**
 * linearize_add_scope_var - Track declared variable in current scope
 * @lctx: Active linearization context
 * @decl: Variable AST declaration
 * @cleanup_fn: Optional GNU cleanup function symbol
 */
static void linearize_add_scope_var(struct linearize_context *lctx,
                                    struct ast_decl *decl,
                                    struct symbol *cleanup_fn)
{
    struct linearize_var *v = NULL;

    if (!lctx->scope || !decl)
        return;

    v = osc_arena_zalloc(&lctx->ctx->arena, sizeof(*v));
    BUG_ON(!v, "arena zalloc failed for linearize_var");

    v->decl = decl;
    v->cleanup_fn = cleanup_fn;
    list_init(&v->node);

    /* Insert at head for LIFO destructor order */
    list_add(&v->node, &lctx->scope->vars);
}

/**
 * linearize_find_var - Search active lexical scopes and parameters for variable
 * @lctx: Active linearization context
 * @id: Symbol name of variable
 *
 * Return: Matching struct ast_decl declaration if found, NULL otherwise.
 */
static struct ast_decl *linearize_find_var(struct linearize_context *lctx,
                                           struct symbol *id)
{
    struct linearize_scope *s = NULL;
    struct linearize_var *v = NULL;
    struct ast_decl *param = NULL;

    if (!lctx || !id)
        return NULL;

    for (s = lctx->scope; s; s = s->prev) {
        list_for_each_entry (v, &s->vars, node) {
            if (v->decl && v->decl->id == id)
                return v->decl;
        }
    }

    if (lctx->fn && lctx->fn->decl && lctx->fn->decl->type &&
        lctx->fn->decl->type->is_function) {
        list_for_each_entry (param, &lctx->fn->decl->type->params, node) {
            if (param->id == id)
                return param;
        }
    }

    return NULL;
}

/**
 * linearize_emit_scope_cleanups - Synthesize OP_CLEANUP for scopes falling out
 * @lctx: Active linearization context
 * @target_scope: Outermost scope to retain (NULL to clear all up to fn entry)
 *
 * Emits OP_CLEANUP instructions in exact reverse declaration order (LIFO)
 * for each variable possessing a __cleanup__ / __free attribute.
 */
static void linearize_emit_scope_cleanups(struct linearize_context *lctx,
                                          struct linearize_scope *target_scope)
{
    struct linearize_scope *s = NULL;
    struct linearize_var *v = NULL;
    struct instruction *insn = NULL;

    for (s = lctx->scope; s && s != target_scope; s = s->prev) {
        list_for_each_entry (v, &s->vars, node) {
            if (v->cleanup_fn && lctx->curr_bb) {
                insn = cfg_insn_new(lctx->fn, OP_CLEANUP, v->decl->span);
                insn->target = cfg_op_var(v->decl);
                insn->callee_sym = v->cleanup_fn;
                cfg_bb_add_insn(lctx->curr_bb, insn);
            }
        }
    }
}

static struct cfg_operand linearize_expr(struct linearize_context *lctx,
                                         struct ast_expr *expr);
static void linearize_stmt(struct linearize_context *lctx,
                           struct ast_stmt *stmt);

/**
 * linearize_expr - Lower an AST expression into 3-address pseudo-instructions
 * @lctx: Active linearization context
 * @expr: AST expression node
 *
 * Return: Result operand (temporary, variable, constant, or symbol).
 */
static struct cfg_operand linearize_expr(struct linearize_context *lctx,
                                         struct ast_expr *expr)
{
    struct instruction *insn = NULL;
    struct instruction *store = NULL;
    struct instruction *br = NULL;
    struct instruction *cbr = NULL;
    struct cfg_operand op = { 0 };
    struct cfg_operand left_op = { 0 };
    struct cfg_operand right_op = { 0 };
    struct cfg_operand cond_op = { 0 };
    struct cfg_operand arg_op = { 0 };
    struct cfg_operand temp = { 0 };
    struct ast_expr *arg = NULL;
    struct basic_block *then_bb = NULL;
    struct basic_block *else_bb = NULL;
    struct basic_block *merge_bb = NULL;
    struct ast_decl *proj_var = NULL;
    struct symbol *proj_mem = NULL;

    if (!expr)
        return op;

    switch (expr->expr_kind) {
    case EXPR_LITERAL:
        if (expr->literal_sym)
            return cfg_op_const(atol(expr->literal_sym->name), expr->type);
        return cfg_op_const(0, expr->type);

    case EXPR_IDENT:
        if (!expr->decl_ref)
            expr->decl_ref = linearize_find_var(lctx, expr->id);
        if (expr->decl_ref)
            return cfg_op_var(expr->decl_ref);
        return cfg_op_sym(expr->id);

    case EXPR_ASSIGN:
        right_op = linearize_expr(lctx, expr->right);
        if (expr->left && expr->left->expr_kind == EXPR_UNARY &&
            expr->left->unary_op == sym_aster) {
            if (expr->left->operand &&
                expr->left->operand->expr_kind == EXPR_MEMBER) {
                left_op = linearize_expr(lctx, expr->left->operand->object);
                insn = cfg_insn_new(lctx->fn, OP_STORE, expr->span);
                insn->target = left_op;
                insn->src1 = right_op;
                insn->container_member = expr->left->operand->member;
                insn->is_deref = true;
                cfg_bb_add_insn(lctx->curr_bb, insn);
                return left_op;
            }
            left_op = linearize_expr(lctx, expr->left->operand);
            insn = cfg_insn_new(lctx->fn, OP_STORE, expr->span);
            insn->target = left_op;
            insn->src1 = right_op;
            insn->is_deref = true;
            cfg_bb_add_insn(lctx->curr_bb, insn);
            return left_op;
        }
        if (expr->left && expr->left->expr_kind == EXPR_MEMBER) {
            left_op = linearize_expr(lctx, expr->left->object);
            insn = cfg_insn_new(lctx->fn, OP_STORE, expr->span);
            insn->target = left_op;
            insn->src1 = right_op;
            insn->container_member = expr->left->member;
            insn->is_deref = false;
            cfg_bb_add_insn(lctx->curr_bb, insn);
            return left_op;
        }
        left_op = linearize_expr(lctx, expr->left);
        insn = cfg_insn_new(lctx->fn, OP_STORE, expr->span);
        insn->target = left_op;
        insn->src1 = right_op;
        insn->is_deref = false;
        cfg_bb_add_insn(lctx->curr_bb, insn);
        return left_op;

    case EXPR_BINARY:
        left_op = linearize_expr(lctx, expr->left);
        right_op = linearize_expr(lctx, expr->right);
        temp = cfg_op_temp(lctx->fn->next_temp_id++, expr->type);

        insn = cfg_insn_new(lctx->fn, OP_LOAD, expr->span);
        insn->target = temp;
        insn->src1 = left_op;
        insn->src2 = right_op;
        insn->op_sym = expr->binary_op;
        cfg_bb_add_insn(lctx->curr_bb, insn);
        return temp;

    case EXPR_UNARY:
        left_op = linearize_expr(lctx, expr->operand);
        temp = cfg_op_temp(lctx->fn->next_temp_id++, expr->type);

        insn = cfg_insn_new(lctx->fn, OP_LOAD, expr->span);
        insn->target = temp;
        insn->src1 = left_op;
        insn->op_sym = expr->unary_op;
        cfg_bb_add_insn(lctx->curr_bb, insn);
        return temp;

    case EXPR_CALL:
        temp = cfg_op_temp(lctx->fn->next_temp_id++, expr->type);
        insn = cfg_insn_new(lctx->fn, OP_CALL, expr->span);
        insn->target = temp;
        if (expr->callee && expr->callee->expr_kind == EXPR_IDENT)
            insn->callee_sym = expr->callee->id;
        insn->is_container_of = expr->is_container_of;

        list_for_each_entry (arg, &expr->args, node) {
            arg_op = linearize_expr(lctx, arg);
            proj_var = NULL;
            proj_mem = NULL;

            if (arg->expr_kind == EXPR_UNARY && arg->unary_op == sym_bit_and &&
                arg->operand && arg->operand->expr_kind == EXPR_MEMBER) {
                if (arg->operand->object &&
                    arg->operand->object->expr_kind == EXPR_IDENT) {
                    proj_var =
                        linearize_find_var(lctx, arg->operand->object->id);
                    proj_mem = arg->operand->member;
                }
            } else if (arg->expr_kind == EXPR_MEMBER) {
                if (arg->object && arg->object->expr_kind == EXPR_IDENT) {
                    proj_var = linearize_find_var(lctx, arg->object->id);
                    proj_mem = arg->member;
                }
            }
            cfg_insn_add_arg_proj(lctx->fn, insn, arg_op, proj_var, proj_mem);
        }

        cfg_bb_add_insn(lctx->curr_bb, insn);
        return temp;

    case EXPR_MEMBER:
        left_op = linearize_expr(lctx, expr->object);
        temp = cfg_op_temp(lctx->fn->next_temp_id++, expr->type);

        insn = cfg_insn_new(lctx->fn, OP_LOAD, expr->span);
        insn->target = temp;
        insn->src1 = left_op;
        insn->container_member = expr->member;
        cfg_bb_add_insn(lctx->curr_bb, insn);
        return temp;

    case EXPR_CAST:
        left_op = linearize_expr(lctx, expr->cast_operand);
        temp = cfg_op_temp(lctx->fn->next_temp_id++, expr->cast_type);

        insn = cfg_insn_new(lctx->fn, OP_LOAD, expr->span);
        insn->target = temp;
        insn->src1 = left_op;
        cfg_bb_add_insn(lctx->curr_bb, insn);
        return temp;

    case EXPR_TERNARY:
        cond_op = linearize_expr(lctx, expr->cond);
        then_bb = cfg_bb_new(lctx->fn, "ternary.then");
        else_bb = cfg_bb_new(lctx->fn, "ternary.else");
        merge_bb = cfg_bb_new(lctx->fn, "ternary.merge");
        temp = cfg_op_temp(lctx->fn->next_temp_id++, expr->type);

        cbr = cfg_insn_new(lctx->fn, OP_CBR, expr->span);
        cbr->src1 = cond_op;
        cbr->true_bb = then_bb;
        cbr->false_bb = else_bb;
        cfg_bb_add_insn(lctx->curr_bb, cbr);
        cfg_add_edge(lctx->fn, lctx->curr_bb, then_bb);
        cfg_add_edge(lctx->fn, lctx->curr_bb, else_bb);

        /* Then branch */
        lctx->curr_bb = then_bb;
        left_op = linearize_expr(lctx, expr->then_expr);
        store = cfg_insn_new(lctx->fn, OP_STORE, expr->span);
        store->target = temp;
        store->src1 = left_op;
        cfg_bb_add_insn(then_bb, store);
        br = cfg_insn_new(lctx->fn, OP_BR, expr->span);
        br->target_bb = merge_bb;
        cfg_bb_add_insn(then_bb, br);
        cfg_add_edge(lctx->fn, then_bb, merge_bb);

        /* Else branch */
        lctx->curr_bb = else_bb;
        right_op = linearize_expr(lctx, expr->else_expr);
        store = cfg_insn_new(lctx->fn, OP_STORE, expr->span);
        store->target = temp;
        store->src1 = right_op;
        cfg_bb_add_insn(else_bb, store);
        br = cfg_insn_new(lctx->fn, OP_BR, expr->span);
        br->target_bb = merge_bb;
        cfg_bb_add_insn(else_bb, br);
        cfg_add_edge(lctx->fn, else_bb, merge_bb);

        lctx->curr_bb = merge_bb;
        return temp;

    case EXPR_STMT_EXPR:
        if (expr->stmt_expr_body)
            linearize_stmt(lctx, expr->stmt_expr_body);
        return cfg_op_temp(lctx->fn->next_temp_id ? lctx->fn->next_temp_id - 1 :
                                                    0,
                           expr->type);

    default:
        return op;
    }
}

/**
 * linearize_stmt - Lower an AST statement node into basic blocks and CFG edges
 * @lctx: Active linearization context
 * @stmt: AST statement node
 */
static void linearize_stmt(struct linearize_context *lctx,
                           struct ast_stmt *stmt)
{
    struct instruction *insn = NULL;
    struct instruction *br = NULL;
    struct instruction *cbr = NULL;
    struct basic_block *hdr_bb = NULL;
    struct basic_block *body_bb = NULL;
    struct basic_block *then_bb = NULL;
    struct basic_block *else_bb = NULL;
    struct basic_block *step_bb = NULL;
    struct basic_block *exit_bb = NULL;
    struct basic_block *merge_bb = NULL;
    struct basic_block *label_bb = NULL;
    struct linearize_scope *new_scope = NULL;
    struct linearize_scope *s = NULL;
    struct linearize_var *v = NULL;
    struct ast_stmt *child = NULL;
    struct cfg_operand cond_op = { 0 };
    struct cfg_operand ret_op = { 0 };
    struct cfg_operand init_op = { 0 };
    struct osc_span scope_span = { 0 };

    if (!stmt || !lctx->curr_bb)
        return;

    switch (stmt->stmt_kind) {
    case STMT_COMPOUND:
        new_scope = osc_arena_zalloc(&lctx->ctx->arena, sizeof(*new_scope));
        BUG_ON(!new_scope, "arena zalloc failed for scope");
        list_init(&new_scope->vars);
        new_scope->break_bb = lctx->scope ? lctx->scope->break_bb : NULL;
        new_scope->continue_bb = lctx->scope ? lctx->scope->continue_bb : NULL;
        new_scope->prev = lctx->scope;
        lctx->scope = new_scope;

        list_for_each_entry (child, &stmt->stmts, node)
            linearize_stmt(lctx, child);

        /* Normal scope fallthrough exits emit cleanups and scope boundaries */
        if (!bb_has_terminator(lctx->curr_bb)) {
            linearize_emit_scope_cleanups(lctx, new_scope->prev);
            scope_span = (stmt->end_span.line > 0) ? stmt->end_span :
                                                     lctx->fn->decl->span;
            list_for_each_entry (v, &new_scope->vars, node) {
                if (v->decl && lctx->curr_bb &&
                    !bb_has_terminator(lctx->curr_bb)) {
                    insn = cfg_insn_new(lctx->fn, OP_SCOPE_END, scope_span);
                    insn->target = cfg_op_var(v->decl);
                    cfg_bb_add_insn(lctx->curr_bb, insn);
                }
            }
        }

        lctx->scope = new_scope->prev;
        break;

    case STMT_DECL:
        if (stmt->stmt_decl) {
            linearize_add_scope_var(lctx, stmt->stmt_decl,
                                    stmt->stmt_decl->cleanup_fn);
            insn = cfg_insn_new(lctx->fn, OP_ALLOC, stmt->stmt_decl->span);
            insn->target = cfg_op_var(stmt->stmt_decl);
            cfg_bb_add_insn(lctx->curr_bb, insn);

            if (stmt->stmt_decl->init) {
                init_op = linearize_expr(lctx, stmt->stmt_decl->init);
                insn = cfg_insn_new(lctx->fn, OP_STORE, stmt->stmt_decl->span);
                insn->target = cfg_op_var(stmt->stmt_decl);
                insn->src1 = init_op;
                cfg_bb_add_insn(lctx->curr_bb, insn);
            }
        }
        break;

    case STMT_EXPR:
        if (stmt->expr)
            linearize_expr(lctx, stmt->expr);
        break;

    case STMT_IF:
        cond_op = linearize_expr(lctx, stmt->if_cond);
        then_bb = cfg_bb_new(lctx->fn, "if.then");
        else_bb = stmt->else_stmt ? cfg_bb_new(lctx->fn, "if.else") : NULL;
        merge_bb = cfg_bb_new(lctx->fn, "if.end");

        cbr = cfg_insn_new(lctx->fn, OP_CBR, stmt->span);
        cbr->src1 = cond_op;
        cbr->true_bb = then_bb;
        cbr->false_bb = else_bb ? else_bb : merge_bb;
        cfg_bb_add_insn(lctx->curr_bb, cbr);
        cfg_add_edge(lctx->fn, lctx->curr_bb, then_bb);
        cfg_add_edge(lctx->fn, lctx->curr_bb, cbr->false_bb);

        /* Then branch */
        lctx->curr_bb = then_bb;
        linearize_stmt(lctx, stmt->then_stmt);
        if (!bb_has_terminator(lctx->curr_bb)) {
            br = cfg_insn_new(lctx->fn, OP_BR, stmt->span);
            br->target_bb = merge_bb;
            cfg_bb_add_insn(lctx->curr_bb, br);
            cfg_add_edge(lctx->fn, lctx->curr_bb, merge_bb);
        }

        /* Else branch */
        if (stmt->else_stmt) {
            lctx->curr_bb = else_bb;
            linearize_stmt(lctx, stmt->else_stmt);
            if (!bb_has_terminator(lctx->curr_bb)) {
                br = cfg_insn_new(lctx->fn, OP_BR, stmt->span);
                br->target_bb = merge_bb;
                cfg_bb_add_insn(lctx->curr_bb, br);
                cfg_add_edge(lctx->fn, lctx->curr_bb, merge_bb);
            }
        }

        lctx->curr_bb = merge_bb;
        break;

    case STMT_WHILE:
        hdr_bb = cfg_bb_new(lctx->fn, "while.cond");
        body_bb = cfg_bb_new(lctx->fn, "while.body");
        exit_bb = cfg_bb_new(lctx->fn, "while.end");

        br = cfg_insn_new(lctx->fn, OP_BR, stmt->span);
        br->target_bb = hdr_bb;
        cfg_bb_add_insn(lctx->curr_bb, br);
        cfg_add_edge(lctx->fn, lctx->curr_bb, hdr_bb);

        /* Loop condition in header */
        lctx->curr_bb = hdr_bb;
        cond_op = linearize_expr(lctx, stmt->loop_cond);
        cbr = cfg_insn_new(lctx->fn, OP_CBR, stmt->span);
        cbr->src1 = cond_op;
        cbr->true_bb = body_bb;
        cbr->false_bb = exit_bb;
        cfg_bb_add_insn(hdr_bb, cbr);
        cfg_add_edge(lctx->fn, hdr_bb, body_bb);
        cfg_add_edge(lctx->fn, hdr_bb, exit_bb);

        /* Loop body */
        new_scope = osc_arena_zalloc(&lctx->ctx->arena, sizeof(*new_scope));
        BUG_ON(!new_scope, "arena zalloc failed for loop scope");
        list_init(&new_scope->vars);
        new_scope->break_bb = exit_bb;
        new_scope->continue_bb = hdr_bb;
        new_scope->prev = lctx->scope;
        lctx->scope = new_scope;

        lctx->curr_bb = body_bb;
        linearize_stmt(lctx, stmt->loop_body);
        if (!bb_has_terminator(lctx->curr_bb)) {
            linearize_emit_scope_cleanups(lctx, new_scope->prev);
            br = cfg_insn_new(lctx->fn, OP_BR, stmt->span);
            br->target_bb = hdr_bb;
            cfg_bb_add_insn(lctx->curr_bb, br);
            cfg_add_edge(lctx->fn, lctx->curr_bb, hdr_bb);
        }

        lctx->scope = new_scope->prev;
        lctx->curr_bb = exit_bb;
        break;

    case STMT_DO_WHILE:
        body_bb = cfg_bb_new(lctx->fn, "dowhile.body");
        hdr_bb = cfg_bb_new(lctx->fn, "dowhile.cond");
        exit_bb = cfg_bb_new(lctx->fn, "dowhile.end");

        br = cfg_insn_new(lctx->fn, OP_BR, stmt->span);
        br->target_bb = body_bb;
        cfg_bb_add_insn(lctx->curr_bb, br);
        cfg_add_edge(lctx->fn, lctx->curr_bb, body_bb);

        new_scope = osc_arena_zalloc(&lctx->ctx->arena, sizeof(*new_scope));
        BUG_ON(!new_scope, "arena zalloc failed for dowhile scope");
        list_init(&new_scope->vars);
        new_scope->break_bb = exit_bb;
        new_scope->continue_bb = hdr_bb;
        new_scope->prev = lctx->scope;
        lctx->scope = new_scope;

        lctx->curr_bb = body_bb;
        linearize_stmt(lctx, stmt->loop_body);
        if (!bb_has_terminator(lctx->curr_bb)) {
            br = cfg_insn_new(lctx->fn, OP_BR, stmt->span);
            br->target_bb = hdr_bb;
            cfg_bb_add_insn(lctx->curr_bb, br);
            cfg_add_edge(lctx->fn, lctx->curr_bb, hdr_bb);
        }

        lctx->scope = new_scope->prev;

        /* Condition check */
        lctx->curr_bb = hdr_bb;
        cond_op = linearize_expr(lctx, stmt->loop_cond);
        cbr = cfg_insn_new(lctx->fn, OP_CBR, stmt->span);
        cbr->src1 = cond_op;
        cbr->true_bb = body_bb;
        cbr->false_bb = exit_bb;
        cfg_bb_add_insn(hdr_bb, cbr);
        cfg_add_edge(lctx->fn, hdr_bb, body_bb);
        cfg_add_edge(lctx->fn, hdr_bb, exit_bb);

        lctx->curr_bb = exit_bb;
        break;

    case STMT_FOR:
        if (stmt->for_init_stmt)
            linearize_stmt(lctx, stmt->for_init_stmt);
        else if (stmt->for_init_expr)
            linearize_expr(lctx, stmt->for_init_expr);

        hdr_bb = cfg_bb_new(lctx->fn, "for.cond");
        body_bb = cfg_bb_new(lctx->fn, "for.body");
        step_bb = cfg_bb_new(lctx->fn, "for.step");
        exit_bb = cfg_bb_new(lctx->fn, "for.end");

        br = cfg_insn_new(lctx->fn, OP_BR, stmt->span);
        br->target_bb = hdr_bb;
        cfg_bb_add_insn(lctx->curr_bb, br);
        cfg_add_edge(lctx->fn, lctx->curr_bb, hdr_bb);

        /* Condition */
        lctx->curr_bb = hdr_bb;
        if (stmt->for_cond) {
            cond_op = linearize_expr(lctx, stmt->for_cond);
            cbr = cfg_insn_new(lctx->fn, OP_CBR, stmt->span);
            cbr->src1 = cond_op;
            cbr->true_bb = body_bb;
            cbr->false_bb = exit_bb;
            cfg_bb_add_insn(hdr_bb, cbr);
            cfg_add_edge(lctx->fn, hdr_bb, body_bb);
            cfg_add_edge(lctx->fn, hdr_bb, exit_bb);
        } else {
            br = cfg_insn_new(lctx->fn, OP_BR, stmt->span);
            br->target_bb = body_bb;
            cfg_bb_add_insn(hdr_bb, br);
            cfg_add_edge(lctx->fn, hdr_bb, body_bb);
        }

        /* Body */
        new_scope = osc_arena_zalloc(&lctx->ctx->arena, sizeof(*new_scope));
        BUG_ON(!new_scope, "arena zalloc failed for for scope");
        list_init(&new_scope->vars);
        new_scope->break_bb = exit_bb;
        new_scope->continue_bb = step_bb;
        new_scope->prev = lctx->scope;
        lctx->scope = new_scope;

        lctx->curr_bb = body_bb;
        linearize_stmt(lctx, stmt->loop_body);
        if (!bb_has_terminator(lctx->curr_bb)) {
            linearize_emit_scope_cleanups(lctx, new_scope->prev);
            br = cfg_insn_new(lctx->fn, OP_BR, stmt->span);
            br->target_bb = step_bb;
            cfg_bb_add_insn(lctx->curr_bb, br);
            cfg_add_edge(lctx->fn, lctx->curr_bb, step_bb);
        }

        lctx->scope = new_scope->prev;

        /* Step */
        lctx->curr_bb = step_bb;
        if (stmt->for_step)
            linearize_expr(lctx, stmt->for_step);

        br = cfg_insn_new(lctx->fn, OP_BR, stmt->span);
        br->target_bb = hdr_bb;
        cfg_bb_add_insn(step_bb, br);
        cfg_add_edge(lctx->fn, step_bb, hdr_bb);

        lctx->curr_bb = exit_bb;
        break;

    case STMT_RETURN:
        if (stmt->ret_expr)
            ret_op = linearize_expr(lctx, stmt->ret_expr);

        /* Emit cleanups for all active scopes up to function entry */
        linearize_emit_scope_cleanups(lctx, NULL);

        insn = cfg_insn_new(lctx->fn, OP_RET, stmt->span);
        insn->src1 = ret_op;
        cfg_bb_add_insn(lctx->curr_bb, insn);
        cfg_add_edge(lctx->fn, lctx->curr_bb, lctx->fn->exit_bb);

        /* Subsequent code in block is dead / unreachable */
        lctx->curr_bb = cfg_bb_new(lctx->fn, "unreachable");
        break;

    case STMT_GOTO:
        label_bb = linearize_find_or_create_label(lctx, stmt->label_name);

        /* Synthesize cleanups for variables being exited */
        linearize_emit_scope_cleanups(lctx, NULL);

        insn = cfg_insn_new(lctx->fn, OP_BR, stmt->span);
        insn->target_bb = label_bb;
        cfg_bb_add_insn(lctx->curr_bb, insn);
        cfg_add_edge(lctx->fn, lctx->curr_bb, label_bb);

        lctx->curr_bb = cfg_bb_new(lctx->fn, "unreachable");
        break;

    case STMT_LABEL:
        label_bb = linearize_find_or_create_label(lctx, stmt->label_name);
        if (!bb_has_terminator(lctx->curr_bb)) {
            br = cfg_insn_new(lctx->fn, OP_BR, stmt->span);
            br->target_bb = label_bb;
            cfg_bb_add_insn(lctx->curr_bb, br);
            cfg_add_edge(lctx->fn, lctx->curr_bb, label_bb);
        }
        lctx->curr_bb = label_bb;
        break;

    case STMT_BREAK:
        for (s = lctx->scope; s; s = s->prev) {
            if (s->break_bb)
                break;
        }
        if (s && s->break_bb) {
            linearize_emit_scope_cleanups(lctx, s);
            br = cfg_insn_new(lctx->fn, OP_BR, stmt->span);
            br->target_bb = s->break_bb;
            cfg_bb_add_insn(lctx->curr_bb, br);
            cfg_add_edge(lctx->fn, lctx->curr_bb, s->break_bb);
        }
        lctx->curr_bb = cfg_bb_new(lctx->fn, "unreachable");
        break;

    case STMT_CONTINUE:
        for (s = lctx->scope; s; s = s->prev) {
            if (s->continue_bb)
                break;
        }
        if (s && s->continue_bb) {
            linearize_emit_scope_cleanups(lctx, s);
            br = cfg_insn_new(lctx->fn, OP_BR, stmt->span);
            br->target_bb = s->continue_bb;
            cfg_bb_add_insn(lctx->curr_bb, br);
            cfg_add_edge(lctx->fn, lctx->curr_bb, s->continue_bb);
        }
        lctx->curr_bb = cfg_bb_new(lctx->fn, "unreachable");
        break;

    case STMT_ASM:
        insn = cfg_insn_new(lctx->fn, OP_ASM, stmt->span);
        insn->asm_code = stmt->asm_code;
        cfg_bb_add_insn(lctx->curr_bb, insn);
        break;

    default:
        break;
    }
}

/**
 * linearize_function - Lower an entire function AST into a Linearized CFG
 * @ctx: Active compilation context
 * @fn_decl: Function definition AST node with body
 *
 * Emits parameter initialization in the entry block, sets up the scope stack,
 * walks the compound body, injects synthesized OP_CLEANUP instructions at
 * all scope exits, and links to the exit block.
 *
 * Return: Pointer to populated struct cfg_function.
 */
struct cfg_function *linearize_function(struct osc_context *ctx,
                                        struct ast_decl *fn_decl)
{
    struct cfg_function *fn = NULL;
    struct linearize_context lctx = { 0 };
    struct ast_decl *param = NULL;
    struct instruction *insn = NULL;

    BUG_ON(!ctx, "null osc_context");
    BUG_ON(!fn_decl, "null fn_decl");

    fn = cfg_function_new(ctx, fn_decl);
    lctx.fn = fn;
    lctx.ctx = ctx;
    lctx.curr_bb = fn->entry_bb;
    lctx.scope = NULL;
    list_init(&lctx.labels);

    /* Emit formal parameter allocations */
    if (fn_decl->type && fn_decl->type->is_function) {
        list_for_each_entry (param, &fn_decl->type->params, node) {
            insn = cfg_insn_new(fn, OP_ENTRY, param->span);
            insn->target = cfg_op_var(param);
            cfg_bb_add_insn(fn->entry_bb, insn);
        }
    }

    /* Linearize function body statements */
    if (fn_decl->body) {
        linearize_stmt(&lctx, fn_decl->body);
        if (fn_decl->body->stmt_kind == STMT_COMPOUND)
            fn->exit_span = fn_decl->body->end_span;
    }

    /* If execution falls through to end, link to exit block */
    if (lctx.curr_bb && !bb_has_terminator(lctx.curr_bb)) {
        linearize_emit_scope_cleanups(&lctx, NULL);
        insn = cfg_insn_new(fn, OP_RET,
                            (fn->exit_span.line > 0) ? fn->exit_span :
                                                       fn_decl->span);
        cfg_bb_add_insn(lctx.curr_bb, insn);
        cfg_add_edge(fn, lctx.curr_bb, fn->exit_bb);
    }

    return fn;
}

/**
 * linearize_translation_unit - Lower all functions in a translation unit to CFGs
 * @ctx: Active compilation context
 * @tu: Parsed AST translation unit
 * @out_cfg_funcs: Destination list head for struct cfg_function nodes
 *
 * Return: Pointer to out_cfg_funcs.
 */
struct list_head *linearize_translation_unit(struct osc_context *ctx,
                                             struct ast_translation_unit *tu,
                                             struct list_head *out_cfg_funcs)
{
    struct ast_decl *decl = NULL;
    struct cfg_function *fn = NULL;

    BUG_ON(!ctx, "null osc_context");
    BUG_ON(!tu, "null ast_translation_unit");
    BUG_ON(!out_cfg_funcs, "null out_cfg_funcs");

    list_for_each_entry (decl, &tu->decls, node) {
        if (decl->type && decl->type->is_function && decl->body) {
            fn = linearize_function(ctx, decl);
            if (fn)
                list_add_tail(&fn->node, out_cfg_funcs);
        }
    }

    return out_cfg_funcs;
}
