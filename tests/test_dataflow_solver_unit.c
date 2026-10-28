#include <osc/parser.h>
#include <osc/check_ownership.h>
#include <osc/cfg.h>
#include <osc/ast.h>
#include <osc/token.h>
#include <osc/context.h>
#include <osc/debug.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

static int g_insn_transfers = 0;
static int g_block_exits = 0;
static bool g_finalized = false;

/**
 * test_init_fn - Initialize function dataflow facts
 * @ctx: Active compilation context
 * @fn: Target CFG function
 * @state: Initial entry block dataflow state
 */
static void test_init_fn(struct osc_context *ctx, struct cfg_function *fn,
                         struct dataflow_state *state)
{
    struct ast_decl *param = NULL;

    if (fn->decl && fn->decl->type && fn->decl->type->is_function) {
        list_for_each_entry (param, &fn->decl->type->params, node) {
            dataflow_state_set_var(state, param, VAR_STATE_SET, param->span);
        }
    }
}

/**
 * test_transfer_insn - Instruction transfer function
 * @ctx: Active compilation context
 * @insn: Instruction being evaluated
 * @state: Mutable block dataflow state
 */
static void test_transfer_insn(struct osc_context *ctx,
                              struct instruction *insn,
                              struct dataflow_state *state)
{
    g_insn_transfers++;

    if (insn->opcode == OP_ALLOC && insn->target.kind == CFG_OPR_VAR) {
        dataflow_state_set_var(state, insn->target.var_decl, VAR_STATE_SET,
                               insn->span);
    } else if (insn->opcode == OP_CLEANUP &&
               insn->target.kind == CFG_OPR_VAR) {
        dataflow_state_set_var(state, insn->target.var_decl, VAR_STATE_DROPPED,
                               insn->span);
    }
}

/**
 * test_join_states - Merge two incoming basic block states
 * @dst: Destination state being merged into
 * @src: Incoming predecessor state
 */
static void test_join_states(struct dataflow_state *dst,
                             const struct dataflow_state *src)
{
    struct dataflow_var_state *v_src = NULL;
    struct dataflow_var_state *v_dst = NULL;

    list_for_each_entry (v_src, &src->var_states, node) {
        v_dst = dataflow_state_get_var(dst, v_src->decl);
        if (!v_dst) {
            dataflow_state_set_var(dst, v_src->decl, v_src->state,
                                   v_src->set_span);
        } else if (v_dst->state != v_src->state) {
            /* Join lattice: higher state wins */
            if (v_src->state > v_dst->state)
                v_dst->state = v_src->state;
        }
    }
}

/**
 * test_check_block_exit - Validate block boundary invariant
 * @ctx: Active compilation context
 * @bb: Basic block ending
 * @state: State at block exit
 */
static void test_check_block_exit(struct osc_context *ctx,
                                 struct basic_block *bb,
                                 struct dataflow_state *state)
{
    g_block_exits++;
}

/**
 * test_finalize_fn - Final post-fixpoint validation
 * @ctx: Active compilation context
 * @fn: Analyzed CFG function
 */
static void test_finalize_fn(struct osc_context *ctx, struct cfg_function *fn)
{
    g_finalized = true;
}

static const struct osc_checker g_test_checker = {
    .name = "test_dataflow_checker",
    .init_function = test_init_fn,
    .transfer_instruction = test_transfer_insn,
    .join_states = test_join_states,
    .check_block_exit = test_check_block_exit,
    .finalize_function = test_finalize_fn,
};

/**
 * tokenize_snippet - Helper to tokenize source code snippet
 * @ctx: Active compilation context
 * @ts: Target token stream
 * @code: Null-terminated C source string
 */
static void tokenize_snippet(struct osc_context *ctx, struct token_stream *ts,
                             const char *code)
{
    FILE *tmp = NULL;

    tmp = tmpfile();
    assert(tmp != NULL);
    fputs(code, tmp);
    rewind(tmp);

    token_stream_init(ts, ctx);
    token_stream_tokenize_file(ts, tmp, "dataflow_snippet.c");
    fclose(tmp);
}

int main(void)
{
    struct file_info fi;
    struct osc_context ctx;
    struct token_stream ts;
    struct ast_translation_unit *tu = NULL;
    struct ast_decl *fn_decl = NULL;
    struct cfg_function *fn = NULL;
    struct basic_block *bb = NULL;
    int ret = 0;
    const char *source =
        "static void cleanup_resource(int *r);\n"
        "\n"
        "int compute_dataflow(int val) {\n"
        "    __attribute__((cleanup(cleanup_resource))) int *res = 0;\n"
        "    int output = 0;\n"
        "\n"
        "    if (val > 10) {\n"
        "        output = val * 2;\n"
        "    } else {\n"
        "        output = val + 5;\n"
        "    }\n"
        "\n"
        "    return output;\n"
        "}\n";

    memset(&fi, 0, sizeof(fi));
    strcpy(fi.name, "test_df.c");

    osc_context_init(&ctx, &fi, OSC_MODE_AUTO);

    tokenize_snippet(&ctx, &ts, source);
    tu = parse_translation_unit(&ts);
    assert(tu != NULL);

    /* Second top-level declaration is compute_dataflow */
    fn_decl = list_first_entry(&tu->decls, struct ast_decl, node);
    assert(fn_decl != NULL);
    fn_decl = list_next_entry(fn_decl, node);
    assert(fn_decl != NULL && fn_decl->body != NULL);

    fn = linearize_function(&ctx, fn_decl);
    assert(fn != NULL);

    /* Run pluggable dataflow fixpoint solver */
    ret = run_dataflow_analysis(&ctx, fn, &g_test_checker);
    assert(ret == 0);

    /* Verify fixpoint solver invoked transfers, exits, and finalizer */
    assert(g_insn_transfers > 0);
    assert(g_block_exits > 0);
    assert(g_finalized == true);

    /* Verify all reachable basic blocks received computed dataflow facts */
    list_for_each_entry (bb, &fn->bbs, node) {
        if (bb == fn->entry_bb || !list_empty(&bb->preds)) {
            assert(bb->df_in != NULL);
            assert(bb->df_out != NULL);
        }
    }

    osc_context_destroy(&ctx);
    symbol_id_container_release();

    printf("Pluggable checker interface and dataflow fixpoint solver tests passed!\n");
    return 0;
}
