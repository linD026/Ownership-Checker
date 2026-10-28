#include <osc/parser.h>
#include <osc/object_ptr.h>
#include <osc/ast.h>
#include <osc/context.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

int main(void)
{
    struct file_info fi;
    struct osc_context ctx;
    struct ast_translation_unit *tu = NULL;
    struct ast_type *type = NULL;
    struct ast_decl *decl = NULL;
    struct ast_expr *left = NULL;
    struct ast_expr *right = NULL;
    struct ast_expr *binary = NULL;
    struct ast_stmt *compound = NULL;
    struct ast_stmt *stmt = NULL;
    struct osc_span span = { .line = 1, .col = 1, .len = 4, .offset = 0 };

    memset(&fi, 0, sizeof(fi));
    strcpy(fi.name, "test_ast.c");

    osc_context_init(&ctx, &fi, OSC_MODE_AUTO);

    /* 1. Translation Unit */
    tu = ast_new_translation_unit(&ctx);
    assert(tu != NULL);
    assert(list_empty(&tu->decls));

    /* 2. Type with qualifiers and OSC __mut attribute */
    type = ast_new_type(&ctx, sym_int, span);
    assert(type != NULL);
    type->qualifiers |= TYPE_QUAL_CONST;
    type->osc_attrs |= ATTR_FLAGS_MUT;
    type->ptr_depth = 1;
    assert(type->ptr_depth == 1);

    /* 3. Declaration with GNU __attribute__((__cleanup__)) */
    decl = ast_new_decl(&ctx, type, NULL, span);
    assert(decl != NULL);
    decl->storage_class = sym_static;
    list_add_tail(&decl->node, &tu->decls);
    assert(!list_empty(&tu->decls));

    /* 4. Expression: Binary operation (left + right) */
    left = ast_new_expr(&ctx, EXPR_LITERAL, span);
    right = ast_new_expr(&ctx, EXPR_LITERAL, span);
    binary = ast_new_expr(&ctx, EXPR_BINARY, span);
    binary->binary_op = sym_add;
    binary->left = left;
    binary->right = right;
    assert(binary->expr_kind == EXPR_BINARY);

    /* 5. Compound Statement */
    compound = ast_new_stmt(&ctx, STMT_COMPOUND, span);
    assert(compound != NULL);
    assert(compound->stmt_kind == STMT_COMPOUND);

    stmt = ast_new_stmt(&ctx, STMT_EXPR, span);
    stmt->expr = binary;
    list_add_tail(&stmt->node, &compound->stmts);
    assert(!list_empty(&compound->stmts));

    /* 6. Context teardown reclaims all arena memory */
    osc_context_destroy(&ctx);

    printf("AST node unit tests passed!\n");
    return 0;
}
