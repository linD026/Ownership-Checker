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
    struct ast_expr *expr = NULL;
    struct ast_decl *decl = NULL;
    struct ast_stmt *first_stmt = NULL;
    struct ast_stmt *second_stmt = NULL;

    memset(&fi, 0, sizeof(fi));
    strcpy(fi.name, "test_pratt.c");

    osc_context_init(&ctx, &fi, OSC_MODE_AUTO);

    /* 1. Precedence: a + b * c */
    tokenize_snippet(&ctx, &ts, "a + b * c;");
    expr = parse_expr(&ts);
    assert(expr != NULL);
    assert(expr->expr_kind == EXPR_BINARY);
    assert(expr->binary_op == sym_add);
    assert(expr->left->expr_kind == EXPR_IDENT);
    assert(strcmp(expr->left->id->name, "a") == 0);
    assert(expr->right->expr_kind == EXPR_BINARY);
    assert(expr->right->binary_op == sym_aster);
    assert(strcmp(expr->right->left->id->name, "b") == 0);
    assert(strcmp(expr->right->right->id->name, "c") == 0);

    /* 2. Right-associative assignment: x = y = z */
    tokenize_snippet(&ctx, &ts, "x = y = z;");
    expr = parse_expr(&ts);
    assert(expr != NULL);
    assert(expr->expr_kind == EXPR_ASSIGN);
    assert(strcmp(expr->left->id->name, "x") == 0);
    assert(expr->right->expr_kind == EXPR_ASSIGN);
    assert(strcmp(expr->right->left->id->name, "y") == 0);
    assert(strcmp(expr->right->right->id->name, "z") == 0);

    /* 3. Function call with address-of & dereference */
    tokenize_snippet(&ctx, &ts, "foo(a, &b, *c);");
    expr = parse_expr(&ts);
    assert(expr != NULL);
    assert(expr->expr_kind == EXPR_CALL);
    assert(strcmp(expr->callee->id->name, "foo") == 0);
    assert(expr->arg_count == 3);

    /* 4. Linux Kernel idioms: container_of and IS_ERR */
    tokenize_snippet(&ctx, &ts, "container_of(ptr, type, member);");
    expr = parse_expr(&ts);
    assert(expr != NULL);
    assert(expr->expr_kind == EXPR_CALL);
    assert(expr->is_container_of == true);

    tokenize_snippet(&ctx, &ts, "IS_ERR(ptr);");
    expr = parse_expr(&ts);
    assert(expr != NULL);
    assert(expr->expr_kind == EXPR_CALL);
    assert(expr->is_err_indicator == true);

    /* 5. Pointer member access & assignment: ptr->field = 10 */
    tokenize_snippet(&ctx, &ts, "ptr->field = 10;");
    expr = parse_expr(&ts);
    assert(expr != NULL);
    assert(expr->expr_kind == EXPR_ASSIGN);
    assert(expr->left->expr_kind == EXPR_MEMBER);
    assert(expr->left->is_arrow == true);
    assert(strcmp(expr->left->member->name, "field") == 0);

    /* 6. GNU Statement Expressions: ({ int x = 1; x + 2; }) */
    tokenize_snippet(&ctx, &ts, "({ int x = 1; x + 2; });");
    expr = parse_expr(&ts);
    assert(expr != NULL);
    assert(expr->expr_kind == EXPR_STMT_EXPR);
    assert(expr->stmt_expr_body != NULL);
    assert(expr->stmt_expr_body->stmt_kind == STMT_COMPOUND);
    assert(!list_empty(&expr->stmt_expr_body->stmts));

    first_stmt = list_first_entry(&expr->stmt_expr_body->stmts, struct ast_stmt, node);
    assert(first_stmt != NULL);
    assert(first_stmt->stmt_kind == STMT_DECL);
    assert(first_stmt->stmt_decl != NULL);
    assert(strcmp(first_stmt->stmt_decl->id->name, "x") == 0);

    second_stmt = list_last_entry(&expr->stmt_expr_body->stmts, struct ast_stmt, node);
    assert(second_stmt != NULL);
    assert(second_stmt->stmt_kind == STMT_EXPR);
    assert(second_stmt->expr->expr_kind == EXPR_BINARY);
    assert(second_stmt->expr->binary_op == sym_add);

    /* 7. Linux Kernel RAII: __attribute__((__cleanup__(put_page))) */
    tokenize_snippet(&ctx, &ts, "struct page *p __attribute__((__cleanup__(put_page))) = alloc();");
    decl = parse_decl(&ts);
    assert(decl != NULL);
    assert(decl->id != NULL);
    assert(strcmp(decl->id->name, "p") == 0);
    assert(decl->cleanup_fn != NULL);
    assert(strcmp(decl->cleanup_fn->name, "put_page") == 0);
    assert(decl->init != NULL);
    assert(decl->init->expr_kind == EXPR_CALL);
    assert(strcmp(decl->init->callee->id->name, "alloc") == 0);

    osc_context_destroy(&ctx);
    symbol_id_container_release();

    printf("Pratt parser & GNU statement expression tests passed!\n");
    return 0;
}
