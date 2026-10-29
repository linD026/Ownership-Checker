#include <osc/parser.h>
#include <osc/cfg.h>
#include <osc/ast.h>
#include <osc/token.h>
#include <osc/context.h>
#include <osc/debug.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

/**
 * tokenize_snippet - Tokenize a C code snippet into a token stream
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
    token_stream_tokenize_file(ts, tmp, "linearize_snippet.c");
    fclose(tmp);
}

int main(void)
{
    struct file_info fi;
    struct osc_context ctx;
    struct token_stream ts;
    struct ast_translation_unit *tu = NULL;
    struct list_head cfg_funcs;
    struct cfg_function *fn = NULL;
    struct basic_block *bb = NULL;
    struct instruction *insn = NULL;
    bool found_cleanup = false;
    bool found_cbr = false;
    int bb_count = 0;
    const char *source =
        "static void my_free(int *p);\n"
        "\n"
        "int test_worker(int count) {\n"
        "    __attribute__((cleanup(my_free))) int *buf = 0;\n"
        "    int sum = 0;\n"
        "\n"
        "    if (count > 0) {\n"
        "        sum = count + 10;\n"
        "    } else {\n"
        "        goto err_out;\n"
        "    }\n"
        "\n"
        "    while (sum > 0) {\n"
        "        sum = sum - 1;\n"
        "        if (sum == 5)\n"
        "            break;\n"
        "    }\n"
        "\n"
        "    return sum;\n"
        "\n"
        "err_out:\n"
        "    return -1;\n"
        "}\n";

    memset(&fi, 0, sizeof(fi));
    strcpy(fi.name, "test_lin.c");

    osc_context_init(&ctx, &fi, OSC_MODE_AUTO);

    tokenize_snippet(&ctx, &ts, source);
    tu = parse_translation_unit(&ts);
    assert(tu != NULL);

    list_init(&cfg_funcs);
    linearize_translation_unit(&ctx, tu, &cfg_funcs);
    assert(!list_empty(&cfg_funcs));

    fn = list_first_entry(&cfg_funcs, struct cfg_function, node);
    assert(fn != NULL);
    assert(fn->name != NULL && strcmp(fn->name->name, "test_worker") == 0);
    assert(fn->entry_bb != NULL);
    assert(fn->exit_bb != NULL);

    /* Verify basic blocks were created */
    list_for_each_entry (bb, &fn->bbs, node) {
        bb_count++;
        list_for_each_entry (insn, &bb->instructions, node) {
            if (insn->opcode == OP_CLEANUP) {
                found_cleanup = true;
                assert(insn->callee_sym != NULL);
                assert(strcmp(insn->callee_sym->name, "my_free") == 0);
            } else if (insn->opcode == OP_CBR) {
                found_cbr = true;
                assert(insn->true_bb != NULL);
                assert(insn->false_bb != NULL);
            }
        }
    }

    /* We expect multiple basic blocks representing if/while/labels */
    assert(bb_count >= 6);
    /* We expect OP_CLEANUP synthesized before exit return */
    assert(found_cleanup == true);
    /* We expect conditional branching */
    assert(found_cbr == true);

    osc_context_destroy(&ctx);
    symbol_id_container_release();

    printf("AST-to-CFG linearization and scope cleanup synthesis tests passed!\n");
    return 0;
}
