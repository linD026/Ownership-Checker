#include <osc/parser.h>
#include <osc/object_ptr.h>
#include <osc/token.h>
#include <osc/ast.h>
#include <osc/context.h>
#include <osc/debug.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

static void tokenize_snippet(struct osc_context *ctx, struct token_stream *ts,
                             const char *code)
{
    FILE *tmp = NULL;

    tmp = tmpfile();
    assert(tmp != NULL);
    fputs(code, tmp);
    rewind(tmp);

    token_stream_init(ts, ctx);
    token_stream_tokenize_file(ts, tmp, "syntax_snippet.c");
    fclose(tmp);
}

int main(void)
{
    struct file_info fi;
    struct osc_context ctx;
    struct token_stream ts;
    struct ast_translation_unit *tu = NULL;
    FILE *dump_fp = NULL;
    char dump_buf[4096];
    size_t bytes_read = 0;
    const char *source_code =
        "_Static_assert(1, \"c11 supported\");\n"
        "\n"
        "struct device {\n"
        "    int id;\n"
        "    union {\n"
        "        int port;\n"
        "        char tag[8];\n"
        "    };\n"
        "};\n"
        "\n"
        "static void cleanup_buf(int *p);\n"
        "\n"
        "int process_data(int count) {\n"
        "    __attribute__((cleanup(cleanup_buf))) int *res = 0;\n"
        "    _Atomic int counter = 0;\n"
        "    int a = 1, *b = 0, c[5];\n"
        "    int val = ({ int temp = count * 2; temp + 1; });\n"
        "\n"
        "    if (count > 0) {\n"
        "        val = _Generic(count, int: count, default: 0);\n"
        "    } else {\n"
        "        goto err_out;\n"
        "    }\n"
        "\n"
        "    for (int i = 0; i < 5; i = i + 1)\n"
        "        c[i] = i;\n"
        "\n"
        "    switch (val) {\n"
        "    case 1:\n"
        "        return 10;\n"
        "    default:\n"
        "        break;\n"
        "    }\n"
        "\n"
        "err_out:\n"
        "    return -1;\n"
        "}\n";

    memset(&fi, 0, sizeof(fi));
    strcpy(fi.name, "syntax_test.c");

    osc_context_init(&ctx, &fi, OSC_MODE_AUTO);

    tokenize_snippet(&ctx, &ts, source_code);
    tu = parse_translation_unit(&ts);
    assert(tu != NULL);
    assert(!list_empty(&tu->decls));

    /* Dump AST into memory / temporary file */
    dump_fp = tmpfile();
    assert(dump_fp != NULL);

    ast_dump_translation_unit(tu, dump_fp);
    rewind(dump_fp);

    bytes_read = fread(dump_buf, 1, sizeof(dump_buf) - 1, dump_fp);
    assert(bytes_read > 0);
    dump_buf[bytes_read] = '\0';
    fclose(dump_fp);

    /* Verify key constructs were emitted by AST dumper */
    assert(strstr(dump_buf, "struct device") != NULL);
    assert(strstr(dump_buf, "union") != NULL);
    assert(strstr(dump_buf, "cleanup_buf") != NULL);
    assert(strstr(dump_buf, "process_data") != NULL);
    assert(strstr(dump_buf, "_Atomic") != NULL);
    assert(strstr(dump_buf, "_Generic") != NULL);
    assert(strstr(dump_buf, "goto err_out") != NULL);
    assert(strstr(dump_buf, "err_out:") != NULL);

    osc_context_destroy(&ctx);
    symbol_id_container_release();

    printf("Full C11 & GNU AST syntax verification and dump tests passed!\n");
    return 0;
}
