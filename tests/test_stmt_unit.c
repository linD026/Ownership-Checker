#include <osc/parser.h>
#include <osc/object_ptr.h>
#include <osc/token.h>
#include <osc/ast.h>
#include <osc/context.h>
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
    token_stream_tokenize_file(ts, tmp, "snippet.c");
    fclose(tmp);
}

int main(void)
{
    struct file_info fi;
    struct osc_context ctx;
    struct token_stream ts;
    struct ast_stmt *stmt = NULL;
    struct ast_stmt *child = NULL;

    memset(&fi, 0, sizeof(fi));
    strcpy(fi.name, "test_stmt.c");

    osc_context_init(&ctx, &fi, OSC_MODE_AUTO);

    /* 1. Deeply nested compound blocks (iterative scope stack test) */
    tokenize_snippet(&ctx, &ts, "{\n"
                                "    {\n"
                                "        {\n"
                                "            int x = 42;\n"
                                "        }\n"
                                "    }\n"
                                "}\n");
    stmt = parse_stmt(&ts);
    assert(stmt != NULL);
    assert(stmt->stmt_kind == STMT_COMPOUND);
    assert(!list_empty(&stmt->stmts));

    child = list_first_entry(&stmt->stmts, struct ast_stmt, node);
    assert(child != NULL);
    assert(child->stmt_kind == STMT_COMPOUND);

    child = list_first_entry(&child->stmts, struct ast_stmt, node);
    assert(child != NULL);
    assert(child->stmt_kind == STMT_COMPOUND);

    child = list_first_entry(&child->stmts, struct ast_stmt, node);
    assert(child != NULL);
    assert(child->stmt_kind == STMT_DECL);
    assert(strcmp(child->stmt_decl->id->name, "x") == 0);

    /* 2. if-else statement */
    tokenize_snippet(&ctx, &ts, "if (x > 0) return 1; else return 0;\n");
    stmt = parse_stmt(&ts);
    assert(stmt != NULL);
    assert(stmt->stmt_kind == STMT_IF);
    assert(stmt->if_cond != NULL);
    assert(stmt->then_stmt != NULL);
    assert(stmt->then_stmt->stmt_kind == STMT_RETURN);
    assert(stmt->else_stmt != NULL);
    assert(stmt->else_stmt->stmt_kind == STMT_RETURN);

    /* 3. while, do-while, and for loops */
    tokenize_snippet(&ctx, &ts, "while (i < 10) i++;\n");
    stmt = parse_stmt(&ts);
    assert(stmt != NULL);
    assert(stmt->stmt_kind == STMT_WHILE);
    assert(stmt->loop_cond != NULL);
    assert(stmt->loop_body != NULL);

    tokenize_snippet(&ctx, &ts, "do { i--; } while (i > 0);\n");
    stmt = parse_stmt(&ts);
    assert(stmt != NULL);
    assert(stmt->stmt_kind == STMT_DO_WHILE);
    assert(stmt->loop_cond != NULL);
    assert(stmt->loop_body != NULL);

    tokenize_snippet(&ctx, &ts, "for (int j = 0; j < 5; j++) continue;\n");
    stmt = parse_stmt(&ts);
    assert(stmt != NULL);
    assert(stmt->stmt_kind == STMT_FOR);
    assert(stmt->for_init_stmt != NULL);
    assert(stmt->for_cond != NULL);
    assert(stmt->for_step != NULL);
    assert(stmt->for_body != NULL);
    assert(stmt->for_body->stmt_kind == STMT_CONTINUE);

    /* 4. switch-case-default and break */
    tokenize_snippet(&ctx, &ts, "switch (val) {\n"
                                "case 1:\n"
                                "    break;\n"
                                "default:\n"
                                "    return -1;\n"
                                "}\n");
    stmt = parse_stmt(&ts);
    assert(stmt != NULL);
    assert(stmt->stmt_kind == STMT_SWITCH);
    assert(stmt->switch_cond != NULL);
    assert(stmt->switch_body != NULL);
    assert(stmt->switch_body->stmt_kind == STMT_COMPOUND);

    /* 5. goto and label */
    tokenize_snippet(&ctx, &ts, "goto err_out;\n");
    stmt = parse_stmt(&ts);
    assert(stmt != NULL);
    assert(stmt->stmt_kind == STMT_GOTO);
    assert(stmt->label_name != NULL);
    assert(strcmp(stmt->label_name->name, "err_out") == 0);

    tokenize_snippet(&ctx, &ts, "err_out: return ret;\n");
    stmt = parse_stmt(&ts);
    assert(stmt != NULL);
    assert(stmt->stmt_kind == STMT_LABEL);
    assert(strcmp(stmt->label_name->name, "err_out") == 0);
    assert(stmt->label_stmt != NULL);
    assert(stmt->label_stmt->stmt_kind == STMT_RETURN);

    /* 6. GNU inline assembly */
    tokenize_snippet(&ctx, &ts, "asm volatile (\"nop\" : : : \"memory\");\n");
    stmt = parse_stmt(&ts);
    assert(stmt != NULL);
    assert(stmt->stmt_kind == STMT_ASM);

    /* 7. C11 _Static_assert */
    tokenize_snippet(&ctx, &ts, "_Static_assert(1, \"always true\");\n");
    stmt = parse_stmt(&ts);
    assert(stmt != NULL);
    assert(stmt->stmt_kind == STMT_STATIC_ASSERT);
    assert(stmt->assert_cond != NULL);
    assert(stmt->assert_msg != NULL);

    osc_context_destroy(&ctx);
    symbol_id_container_release();

    printf("Iterative statement parser unit tests passed!\n");
    return 0;
}
