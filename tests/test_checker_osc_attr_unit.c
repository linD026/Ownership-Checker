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

/**
 * tokenize_snippet - Helper to tokenize source code snippet into stream
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
    token_stream_tokenize_file(ts, tmp, "osc_attr_snippet.c");
    fclose(tmp);
}

/**
 * run_checker_on_code - Helper to parse, linearize, and run checker_osc_attr
 * @code: Null-terminated C source string
 * @fn_name: Target function name to verify
 * @out_error_count: Destination for number of detected errors
 */
static void run_checker_on_code(const char *code, const char *fn_name,
                                unsigned int *out_error_count)
{
    struct file_info fi;
    struct osc_context ctx;
    struct token_stream ts;
    struct ast_translation_unit *tu = NULL;
    struct ast_decl *decl = NULL;
    struct ast_decl *target_fn = NULL;
    struct cfg_function *fn = NULL;
    int ret = 0;

    memset(&fi, 0, sizeof(fi));
    strcpy(fi.name, "test_osc_attr.c");

    osc_context_init(&ctx, &fi, OSC_MODE_OSC);

    tokenize_snippet(&ctx, &ts, code);
    tu = parse_translation_unit(&ts);
    assert(tu != NULL);

    /* Locate the target function */
    list_for_each_entry (decl, &tu->decls, node) {
        if (decl->type && decl->type->is_function && decl->body &&
            decl->id && !strcmp(decl->id->name, fn_name)) {
            target_fn = decl;
            break;
        }
    }

    assert(target_fn != NULL);

    fn = linearize_function(&ctx, target_fn);
    assert(fn != NULL);

    ret = run_dataflow_analysis(&ctx, fn, &checker_osc_attr);
    assert(ret == 0);

    if (out_error_count)
        *out_error_count = ctx.error_count;

    osc_context_destroy(&ctx);
    symbol_id_container_release();
}

/**
 * test_borrow_write - Verify write to borrowed reference is caught
 */
static void test_borrow_write(void)
{
    unsigned int errs = 0;
    const char *code =
        "void test_fn(int * __brw b, int *other) {\n"
        "    *b = 10;\n"
        "    b = other;\n"
        "}\n";

    run_checker_on_code(code, "test_fn", &errs);
    /* Should flag exactly 1 error on *b = 10, b = other is allowed */
    assert(errs == 1);
}

/**
 * test_move_and_use_after_drop - Verify call move semantics and post-drop write
 */
static void test_move_and_use_after_drop(void)
{
    unsigned int errs = 0;
    const char *code =
        "void consume(int * __mut p);\n"
        "\n"
        "void test_fn(int * __mut m) {\n"
        "    consume(m);\n"
        "    *m = 20;\n"
        "}\n";

    run_checker_on_code(code, "test_fn", &errs);
    /* consume(m) moves m; *m = 20 writes to dropped object */
    assert(errs == 1);
}

/**
 * test_return_borrowed - Verify returning borrowed object is rejected
 */
static void test_return_borrowed(void)
{
    unsigned int errs = 0;
    const char *code =
        "int *test_fn(int * __brw b) {\n"
        "    return b;\n"
        "}\n";

    run_checker_on_code(code, "test_fn", &errs);
    assert(errs == 1);
}

/**
 * test_return_dropped - Verify returning dropped object is rejected
 */
static void test_return_dropped(void)
{
    unsigned int errs = 0;
    const char *code =
        "void consume(int * __mut p);\n"
        "\n"
        "int *test_fn(int * __mut m) {\n"
        "    consume(m);\n"
        "    return m;\n"
        "}\n";

    run_checker_on_code(code, "test_fn", &errs);
    assert(errs == 1);
}

/**
 * test_unreleased_scoped - Verify unreleased local mutable pointer at scope exit
 */
static void test_unreleased_scoped(void)
{
    unsigned int errs = 0;
    const char *code =
        "void test_fn(int *x) {\n"
        "    {\n"
        "        int * __mut local_mut = x;\n"
        "        *local_mut = 5;\n"
        "    }\n"
        "}\n";

    run_checker_on_code(code, "test_fn", &errs);
    /* local_mut ends scope without being dropped */
    assert(errs == 1);
}

/**
 * test_unreleased_param - Verify unreleased parameter at function exit
 */
static void test_unreleased_param(void)
{
    unsigned int errs = 0;
    const char *code =
        "void test_fn(int * __mut p) {\n"
        "    *p = 42;\n"
        "}\n";

    run_checker_on_code(code, "test_fn", &errs);
    /* p is not released before function exit */
    assert(errs == 1);
}

/**
 * test_branch_join_lattice - Verify dropped state dominates across joined branches
 */
static void test_branch_join_lattice(void)
{
    unsigned int errs = 0;
    const char *code =
        "void release(int *p);\n"
        "\n"
        "void test_fn(int * __mut p, int cond) {\n"
        "    if (cond) {\n"
        "        release(p);\n"
        "    } else {\n"
        "        *p = 1;\n"
        "    }\n"
        "    *p = 2;\n"
        "}\n";

    run_checker_on_code(code, "test_fn", &errs);
    /* At *p = 2, p is potentially dropped -> error! */
    assert(errs >= 1);
}

/**
 * test_valid_move_and_return - Verify valid ownership transfer produces zero errors
 */
static void test_valid_move_and_return(void)
{
    unsigned int errs = 0;
    const char *code =
        "int *test_fn(int * __mut p) {\n"
        "    *p = 100;\n"
        "    return p;\n"
        "}\n";

    run_checker_on_code(code, "test_fn", &errs);
    /* Returning p transfers ownership to caller; zero errors */
    assert(errs == 0);
}

int main(void)
{
    test_borrow_write();
    test_move_and_use_after_drop();
    test_return_borrowed();
    test_return_dropped();
    test_unreleased_scoped();
    test_unreleased_param();
    test_branch_join_lattice();
    test_valid_move_and_return();

    printf("Model 1 checker_osc_attr unit tests passed successfully!\n");
    return 0;
}
