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
    token_stream_tokenize_file(ts, tmp, "cfg_dump_snippet.c");
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
    FILE *fp_txt = NULL;
    FILE *fp_dot = NULL;
    char buf_txt[8192];
    char buf_dot[8192];
    size_t nr_txt = 0;
    size_t nr_dot = 0;
    const char *source =
        "static void release_handle(int *h);\n"
        "\n"
        "int process_records(int limit) {\n"
        "    __attribute__((cleanup(release_handle))) int *handle = 0;\n"
        "    int idx = 0;\n"
        "\n"
        "    if (limit > 0) {\n"
        "        idx = limit * 2;\n"
        "    } else {\n"
        "        return -1;\n"
        "    }\n"
        "\n"
        "    while (idx > 0) {\n"
        "        idx = idx - 1;\n"
        "        if (idx == 10)\n"
        "            break;\n"
        "    }\n"
        "\n"
        "    return idx;\n"
        "}\n";

    memset(&fi, 0, sizeof(fi));
    strcpy(fi.name, "test_cfg_dump.c");

    osc_context_init(&ctx, &fi, OSC_MODE_AUTO);

    tokenize_snippet(&ctx, &ts, source);
    tu = parse_translation_unit(&ts);
    assert(tu != NULL);

    /* Second top-level declaration is process_records */
    fn_decl = list_first_entry(&tu->decls, struct ast_decl, node);
    assert(fn_decl != NULL);
    fn_decl = list_next_entry(fn_decl, node);
    assert(fn_decl != NULL && fn_decl->body != NULL);

    fn = linearize_function(&ctx, fn_decl);
    assert(fn != NULL);

    /* 1. Test Textual CFG Dump */
    fp_txt = tmpfile();
    assert(fp_txt != NULL);
    cfg_dump_text(fn, fp_txt);
    rewind(fp_txt);

    nr_txt = fread(buf_txt, 1, sizeof(buf_txt) - 1, fp_txt);
    assert(nr_txt > 0);
    buf_txt[nr_txt] = '\0';
    fclose(fp_txt);

    /* Verify textual markers */
    assert(strstr(buf_txt, "function process_records:") != NULL);
    assert(strstr(buf_txt, "BB0") != NULL);
    assert(strstr(buf_txt, "; preds:") != NULL);
    assert(strstr(buf_txt, "; succs:") != NULL);
    assert(strstr(buf_txt, "store") != NULL);
    assert(strstr(buf_txt, "cbr") != NULL);
    assert(strstr(buf_txt, "cleanup release_handle") != NULL);
    assert(strstr(buf_txt, "ret") != NULL);

    /* 2. Test Graphviz DOT Dump */
    fp_dot = tmpfile();
    assert(fp_dot != NULL);
    cfg_dump_dot(fn, fp_dot);
    rewind(fp_dot);

    nr_dot = fread(buf_dot, 1, sizeof(buf_dot) - 1, fp_dot);
    assert(nr_dot > 0);
    buf_dot[nr_dot] = '\0';
    fclose(fp_dot);

    /* Verify Graphviz DOT markers */
    assert(strstr(buf_dot, "digraph \"process_records\"") != NULL);
    assert(strstr(buf_dot, "node [shape=record") != NULL);
    assert(strstr(buf_dot, "bb_0 [label=\"{BB0") != NULL);
    assert(strstr(buf_dot, "->") != NULL);
    assert(strstr(buf_dot, "label=\"true\"") != NULL);
    assert(strstr(buf_dot, "label=\"false\"") != NULL);

    osc_context_destroy(&ctx);
    symbol_id_container_release();

    printf("CFG textual listing and Graphviz DOT dumper tests passed!\n");
    return 0;
}
