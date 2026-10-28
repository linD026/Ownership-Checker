#include <osc/parser.h>
#include <osc/cfg.h>
#include <osc/ast.h>
#include <osc/context.h>
#include <osc/debug.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

int main(void)
{
    struct file_info fi;
    struct osc_context ctx;
    struct cfg_function *fn = NULL;
    struct basic_block *bb_loop = NULL;
    struct basic_block *bb_body = NULL;
    struct instruction *insn_alloc = NULL;
    struct instruction *insn_call = NULL;
    struct instruction *insn_cbr = NULL;
    struct instruction *insn_cleanup = NULL;
    struct cfg_edge *edge1 = NULL;
    struct cfg_edge *edge_back = NULL;
    struct osc_span span = { .line = 1, .col = 1, .len = 5, .offset = 0 };

    memset(&fi, 0, sizeof(fi));
    strcpy(fi.name, "test_cfg.c");

    osc_context_init(&ctx, &fi, OSC_MODE_AUTO);

    /* 1. Create CFG function */
    fn = cfg_function_new(&ctx, NULL);
    assert(fn != NULL);
    assert(fn->entry_bb != NULL && fn->entry_bb->id == 0);
    assert(fn->exit_bb != NULL && fn->exit_bb->id == 1);
    assert(!list_empty(&fn->bbs));

    /* 2. Create additional basic blocks */
    bb_loop = cfg_bb_new(fn, "loop.header");
    assert(bb_loop != NULL && bb_loop->id == 2);
    assert(bb_loop->func == fn);

    bb_body = cfg_bb_new(fn, "loop.body");
    assert(bb_body != NULL && bb_body->id == 3);
    assert(bb_body->func == fn);

    /* 3. Build instructions in entry block */
    insn_alloc = cfg_insn_new(fn, OP_ALLOC, span);
    insn_alloc->target = cfg_op_temp(0, NULL);
    cfg_bb_add_insn(fn->entry_bb, insn_alloc);
    assert(!list_empty(&fn->entry_bb->instructions));
    assert(insn_alloc->bb == fn->entry_bb);

    /* Call instruction with arguments */
    insn_call = cfg_insn_new(fn, OP_CALL, span);
    insn_call->target = cfg_op_temp(1, NULL);
    cfg_insn_add_arg(fn, insn_call, cfg_op_const(42, NULL));
    cfg_insn_add_arg(fn, insn_call, cfg_op_temp(0, NULL));
    assert(insn_call->arg_count == 2);
    assert(!list_empty(&insn_call->args));
    cfg_bb_add_insn(fn->entry_bb, insn_call);

    /* Conditional branch in loop header */
    insn_cbr = cfg_insn_new(fn, OP_CBR, span);
    insn_cbr->src1 = cfg_op_temp(1, NULL);
    insn_cbr->true_bb = bb_body;
    insn_cbr->false_bb = fn->exit_bb;
    cfg_bb_add_insn(bb_loop, insn_cbr);

    /* Cleanup instruction in loop body */
    insn_cleanup = cfg_insn_new(fn, OP_CLEANUP, span);
    insn_cleanup->target = cfg_op_temp(0, NULL);
    cfg_bb_add_insn(bb_body, insn_cleanup);

    /* 4. Connect CFG edges */
    edge1 = cfg_add_edge(fn, fn->entry_bb, bb_loop);
    assert(edge1 != NULL);
    assert(edge1->src == fn->entry_bb);
    assert(edge1->dst == bb_loop);
    assert(edge1->is_backedge == false);
    assert(!list_empty(&fn->entry_bb->succs));
    assert(!list_empty(&bb_loop->preds));

    /* Backedge from loop body (id 3) to loop header (id 2) */
    edge_back = cfg_add_edge(fn, bb_body, bb_loop);
    assert(edge_back != NULL);
    assert(edge_back->is_backedge == true);

    osc_context_destroy(&ctx);
    symbol_id_container_release();

    printf("CFG IR data structures and basic block unit tests passed!\n");
    return 0;
}
