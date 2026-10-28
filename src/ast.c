#include <osc/ast.h>
#include <osc/context.h>
#include <osc/debug.h>
#include <string.h>

struct ast_type *ast_new_type(struct osc_context *ctx, int base_type,
                              struct osc_span span)
{
    struct ast_type *type = NULL;

    BUG_ON(!ctx, "null context");

    type = osc_arena_zalloc(&ctx->arena, sizeof(*type));
    BUG_ON(!type, "arena zalloc failed for ast_type");

    type->kind = AST_NODE_TYPE;
    type->span = span;
    type->base_type = base_type;
    list_init(&type->params);

    return type;
}

struct ast_decl *ast_new_decl(struct osc_context *ctx, struct ast_type *type,
                              struct symbol *id, struct osc_span span)
{
    struct ast_decl *decl = NULL;

    BUG_ON(!ctx, "null context");

    decl = osc_arena_zalloc(&ctx->arena, sizeof(*decl));
    BUG_ON(!decl, "arena zalloc failed for ast_decl");

    decl->kind = AST_NODE_DECL;
    decl->span = span;
    decl->type = type;
    decl->id = id;
    list_init(&decl->node);

    return decl;
}

struct ast_expr *ast_new_expr(struct osc_context *ctx, enum ast_expr_kind kind,
                              struct osc_span span)
{
    struct ast_expr *expr = NULL;

    BUG_ON(!ctx, "null context");

    expr = osc_arena_zalloc(&ctx->arena, sizeof(*expr));
    BUG_ON(!expr, "arena zalloc failed for ast_expr");

    expr->kind = AST_NODE_EXPR;
    expr->expr_kind = kind;
    expr->span = span;
    list_init(&expr->node);

    if (kind == EXPR_CALL)
        list_init(&expr->args);

    return expr;
}

struct ast_stmt *ast_new_stmt(struct osc_context *ctx, enum ast_stmt_kind kind,
                              struct osc_span span)
{
    struct ast_stmt *stmt = NULL;

    BUG_ON(!ctx, "null context");

    stmt = osc_arena_zalloc(&ctx->arena, sizeof(*stmt));
    BUG_ON(!stmt, "arena zalloc failed for ast_stmt");

    stmt->kind = AST_NODE_STMT;
    stmt->stmt_kind = kind;
    stmt->span = span;
    list_init(&stmt->node);

    if (kind == STMT_COMPOUND) {
        list_init(&stmt->stmts);
        list_init(&stmt->decls);
    }

    return stmt;
}

struct ast_translation_unit *ast_new_translation_unit(struct osc_context *ctx)
{
    struct ast_translation_unit *tu = NULL;

    BUG_ON(!ctx, "null context");

    tu = osc_arena_zalloc(&ctx->arena, sizeof(*tu));
    BUG_ON(!tu, "arena zalloc failed for ast_translation_unit");

    tu->ctx = ctx;
    list_init(&tu->decls);

    return tu;
}
