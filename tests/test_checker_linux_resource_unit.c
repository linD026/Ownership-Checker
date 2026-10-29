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
    token_stream_tokenize_file(ts, tmp, "linux_resource_snippet.c");
    fclose(tmp);
}

/**
 * run_checker_on_code - Helper to parse, linearize, and run checker_linux_resource
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
    strcpy(fi.name, "test_linux_res.c");

    osc_context_init(&ctx, &fi, OSC_MODE_KERNEL);

    tokenize_snippet(&ctx, &ts, code);
    tu = parse_translation_unit(&ts);
    assert(tu != NULL);

    /* Locate target function declaration */
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

    ret = run_dataflow_analysis(&ctx, fn, &checker_linux_resource);
    assert(ret == 0);

    if (out_error_count)
        *out_error_count = ctx.error_count;

    osc_context_destroy(&ctx);
    symbol_id_container_release();
}

/**
 * test_destructor_after_transfer - Catch escaped object without disarming
 */
static void test_destructor_after_transfer(void)
{
    unsigned int errs = 0;
    const char *code =
        "static void put_page(void *p);\n"
        "static void *alloc_page(int flags);\n"
        "static void list_add(void *new, void *head);\n"
        "\n"
        "void *transfer_page(void *global_list) {\n"
        "    __attribute__((cleanup(put_page))) void *p = alloc_page(0);\n"
        "    list_add(p, global_list);\n"
        "    return p;\n"
        "}\n";

    run_checker_on_code(code, "transfer_page", &errs);
    /* Flags Destructor-after-transfer: p escaped to list_add but was not disarmed */
    assert(errs == 1);
}

/**
 * test_disarmed_escape - Verify properly disarmed escape produces zero errors
 */
static void test_disarmed_escape(void)
{
    unsigned int errs = 0;
    const char *code =
        "static void put_page(void *p);\n"
        "static void *alloc_page(int flags);\n"
        "static void list_add(void *new, void *head);\n"
        "\n"
        "int transfer_page_ok(void *global_list) {\n"
        "    __attribute__((cleanup(put_page))) void *p = alloc_page(0);\n"
        "    list_add(p, global_list);\n"
        "    p = 0;\n"
        "    return 0;\n"
        "}\n";

    run_checker_on_code(code, "transfer_page_ok", &errs);
    /* p = 0 successfully disarmed cleanup: 0 errors */
    assert(errs == 0);
}

/**
 * test_hybrid_double_free - Catch manual kfree coexisting with __free on goto
 */
static void test_hybrid_double_free(void)
{
    unsigned int errs = 0;
    const char *code =
        "static void kfree(void *p);\n"
        "static void *kmalloc(int size, int flags);\n"
        "\n"
        "void hybrid_error_path(int err) {\n"
        "    __attribute__((cleanup(kfree))) void *p = kmalloc(32, 0);\n"
        "    if (err) {\n"
        "        kfree(p);\n"
        "        goto out;\n"
        "    }\n"
        "out:\n"
        "    return;\n"
        "}\n";

    run_checker_on_code(code, "hybrid_error_path", &errs);
    /* kfree(p) followed by synthesized cleanup fires double-free */
    assert(errs >= 1);
}

/**
 * test_intrusive_container_transfer - Verify sub-object projection relieves leak
 */
static void test_intrusive_container_transfer(void)
{
    unsigned int errs = 0;
    const char *code =
        "struct list_head { void *next; void *prev; };\n"
        "struct task_struct { struct list_head tasks; int pid; };\n"
        "static void list_add_tail(struct list_head *new, struct list_head *head);\n"
        "static void *kmalloc(int size, int flags);\n"
        "\n"
        "int enqueue_task(struct list_head *task_list) {\n"
        "    struct task_struct *task = kmalloc(64, 0);\n"
        "    list_add_tail(&task->tasks, task_list);\n"
        "    return 0;\n"
        "}\n";

    run_checker_on_code(code, "enqueue_task", &errs);
    /* Container task ownership transferred to task_list; no leak */
    assert(errs == 0);
}

/**
 * test_error_path_leak - Detect unreleased allocation on error exit
 */
static void test_error_path_leak(void)
{
    unsigned int errs = 0;
    const char *code =
        "static void kfree(void *p);\n"
        "static void *kmalloc(int size, int flags);\n"
        "\n"
        "int alloc_with_leak(int err) {\n"
        "    void *p = kmalloc(64, 0);\n"
        "    if (err) {\n"
        "        return -12;\n"
        "    }\n"
        "    kfree(p);\n"
        "    return 0;\n"
        "}\n";

    run_checker_on_code(code, "alloc_with_leak", &errs);
    /* p leaked on error return path */
    assert(errs >= 1);
}

/**
 * test_standard_double_free - Detect sequential double deallocation
 */
static void test_standard_double_free(void)
{
    unsigned int errs = 0;
    const char *code =
        "static void kfree(void *p);\n"
        "static void *kmalloc(int size, int flags);\n"
        "\n"
        "void double_free_fn(void) {\n"
        "    void *p = kmalloc(32, 0);\n"
        "    kfree(p);\n"
        "    kfree(p);\n"
        "}\n";

    run_checker_on_code(code, "double_free_fn", &errs);
    /* Second kfree flags double-free */
    assert(errs == 1);
}

int main(void)
{
    test_destructor_after_transfer();
    test_disarmed_escape();
    test_hybrid_double_free();
    test_intrusive_container_transfer();
    test_error_path_leak();
    test_standard_double_free();

    printf("Model 2 checker_linux_resource unit tests passed successfully!\n");
    return 0;
}
