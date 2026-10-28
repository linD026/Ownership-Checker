#include <osc/ast.h>
#include <osc/context.h>
#include <osc/debug.h>
#include <osc/object_ptr.h>
#include <osc/parser.h>
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

static bool is_type_token(int sym)
{
    if (range_in_sym(type, sym))
        return true;
    if (range_in_sym(qualifier, sym))
        return true;
    if (range_in_sym(storage_class, sym))
        return true;
    if (range_in_sym(attr, sym))
        return true;
    if (sym == sym___attribute__ || sym == sym_inline ||
        sym == sym___inline__ || sym == sym__Noreturn)
        return true;

    return false;
}

static enum expr_precedence get_infix_precedence(int sym)
{
    switch (sym) {
    case sym_comma:
        return PREC_COMMA;
    case sym_eq:
        return PREC_ASSIGN;
    case sym_query:
        return PREC_TERNARY;
    case sym_logic_or:
        return PREC_LOGICAL_OR;
    case sym_logic_and:
        return PREC_LOGICAL_AND;
    case sym_bit_or:
        return PREC_BIT_OR;
    case sym_bit_xor:
        return PREC_BIT_XOR;
    case sym_bit_and:
        return PREC_BIT_AND;
    case sym_equal:
    case sym_not_equal:
        return PREC_EQUALITY;
    case sym_lt:
    case sym_le:
    case sym_gt:
    case sym_ge:
        return PREC_RELATIONAL;
    case sym_bit_left:
    case sym_bit_right:
        return PREC_SHIFT;
    case sym_add:
    case sym_minus:
        return PREC_ADDITIVE;
    case sym_aster:
    case sym_slash:
    case sym_percent:
        return PREC_MULTIPLICATIVE;
    case sym_left_paren:
    case sym_left_sq_brace:
    case sym_dot:
    case sym_ptr_assign:
    case sym_inc:
    case sym_dec:
        return PREC_POSTFIX;
    default:
        return PREC_NONE;
    }
}

struct ast_type *parse_type(struct token_stream *ts)
{
    struct ast_type *type = NULL;
    struct osc_token *tok = NULL;
    struct osc_span span = { 0 };
    unsigned int qualifiers = 0;
    unsigned int osc_attrs = 0;
    unsigned int ptr_depth = 0;
    int base_type = sym_int;
    struct symbol *tag = NULL;

    BUG_ON(!ts, "null token_stream");

    tok = tok_peek(ts, 0);
    if (tok)
        span = tok->span;

    /* Parse leading qualifiers and attributes */
    while (1) {
        tok = tok_peek(ts, 0);
        if (!tok || tok->sym == sym_dump)
            break;

        if (tok->sym == sym_const) {
            qualifiers |= TYPE_QUAL_CONST;
            tok_consume(ts);
        } else if (tok->sym == sym_volatile) {
            qualifiers |= TYPE_QUAL_VOLATILE;
            tok_consume(ts);
        } else if (tok->sym == sym_restrict) {
            qualifiers |= TYPE_QUAL_RESTRICT;
            tok_consume(ts);
        } else if (tok->sym == sym__Atomic) {
            qualifiers |= TYPE_QUAL_ATOMIC;
            tok_consume(ts);
        } else if (tok->sym == sym_attr_mut) {
            osc_attrs |= ATTR_FLAGS_MUT;
            tok_consume(ts);
        } else if (tok->sym == sym_attr_brw) {
            osc_attrs |= ATTR_FLAGS_BRW;
            tok_consume(ts);
        } else if (tok->sym == sym_attr_clone) {
            osc_attrs |= ATTR_FLAGS_CLONE;
            tok_consume(ts);
        } else {
            break;
        }
    }

    /* Parse base type */
    tok = tok_peek(ts, 0);
    if (tok && range_in_sym(type, tok->sym)) {
        base_type = tok->sym;
        tok_consume(ts);

        if (base_type == sym_struct) {
            tok = tok_peek(ts, 0);
            if (tok && tok->sym == sym_id) {
                tag = tok->symbol;
                tok_consume(ts);
            }
        }
    }

    /* Parse pointers and pointer-level attributes */
    while (1) {
        tok = tok_peek(ts, 0);
        if (!tok || tok->sym != sym_aster)
            break;

        tok_consume(ts);
        ptr_depth++;

        /* Check for attributes/qualifiers on pointer */
        tok = tok_peek(ts, 0);
        if (tok && tok->sym == sym_attr_mut) {
            osc_attrs |= ATTR_FLAGS_MUT;
            tok_consume(ts);
        } else if (tok && tok->sym == sym_attr_brw) {
            osc_attrs |= ATTR_FLAGS_BRW;
            tok_consume(ts);
        } else if (tok && tok->sym == sym_const) {
            qualifiers |= TYPE_QUAL_CONST;
            tok_consume(ts);
        }
    }

    type = ast_new_type(ts->ctx, base_type, span);
    type->qualifiers = qualifiers;
    type->osc_attrs = osc_attrs;
    type->ptr_depth = ptr_depth;
    type->tag = tag;

    return type;
}

static struct symbol *parse_gnu_attribute_cleanup(struct token_stream *ts)
{
    struct osc_token *tok = NULL;
    struct symbol *cleanup_fn = NULL;

    BUG_ON(!ts, "null token_stream");

    /* Expect: __attribute__ ( ( cleanup ( func ) ) ) */
    if (!tok_match(ts, sym___attribute__))
        return NULL;

    if (!tok_match(ts, sym_left_paren))
        return NULL;
    if (!tok_match(ts, sym_left_paren))
        return NULL;

    tok = tok_peek(ts, 0);
    if (tok && tok->symbol &&
        (strcmp(tok->symbol->name, "cleanup") == 0 ||
         strcmp(tok->symbol->name, "__cleanup__") == 0)) {
        tok_consume(ts);
        if (tok_match(ts, sym_left_paren)) {
            tok = tok_peek(ts, 0);
            if (tok && tok->sym == sym_id) {
                cleanup_fn = tok->symbol;
                tok_consume(ts);
            }
            tok_match(ts, sym_right_paren);
        }
    }

    /* Skip until matching double right parens */
    while (1) {
        tok = tok_peek(ts, 0);
        if (!tok || tok->sym == sym_dump || tok->sym == sym_seq_point)
            break;
        if (tok->sym == sym_right_paren) {
            tok_consume(ts);
            if (tok_peek(ts, 0) && tok_peek(ts, 0)->sym == sym_right_paren) {
                tok_consume(ts);
                break;
            }
        } else {
            tok_consume(ts);
        }
    }

    return cleanup_fn;
}

struct ast_decl *parse_decl(struct token_stream *ts)
{
    struct ast_type *type = NULL;
    struct ast_decl *decl = NULL;
    struct osc_token *tok = NULL;
    struct symbol *id = NULL;
    struct symbol *cleanup_fn = NULL;
    struct ast_expr *init_expr = NULL;
    struct osc_span span = { 0 };
    int storage_class = 0;
    bool is_inline = false;
    bool is_noreturn = false;

    BUG_ON(!ts, "null token_stream");

    tok = tok_peek(ts, 0);
    if (tok)
        span = tok->span;

    /* Parse storage class and function specifiers */
    while (1) {
        tok = tok_peek(ts, 0);
        if (!tok || tok->sym == sym_dump)
            break;

        if (range_in_sym(storage_class, tok->sym)) {
            storage_class = tok->sym;
            tok_consume(ts);
        } else if (tok->sym == sym_inline || tok->sym == sym___inline__) {
            is_inline = true;
            tok_consume(ts);
        } else if (tok->sym == sym__Noreturn) {
            is_noreturn = true;
            tok_consume(ts);
        } else {
            break;
        }
    }

    /* GNU attributes before type */
    if (tok_peek(ts, 0) && tok_peek(ts, 0)->sym == sym___attribute__)
        cleanup_fn = parse_gnu_attribute_cleanup(ts);

    type = parse_type(ts);

    /* GNU attributes after type / before name */
    if (!cleanup_fn && tok_peek(ts, 0) &&
        tok_peek(ts, 0)->sym == sym___attribute__)
        cleanup_fn = parse_gnu_attribute_cleanup(ts);

    tok = tok_peek(ts, 0);
    if (tok && tok->sym == sym_id) {
        id = tok->symbol;
        tok_consume(ts);
    }

    /* GNU attributes after name */
    if (!cleanup_fn && tok_peek(ts, 0) &&
        tok_peek(ts, 0)->sym == sym___attribute__)
        cleanup_fn = parse_gnu_attribute_cleanup(ts);

    /* Optional initializer */
    if (tok_match(ts, sym_eq))
        init_expr = parse_expr_precedence(ts, PREC_ASSIGN);

    decl = ast_new_decl(ts->ctx, type, id, span);
    decl->storage_class = storage_class;
    decl->is_inline = is_inline;
    decl->is_noreturn = is_noreturn;
    decl->cleanup_fn = cleanup_fn;
    decl->init = init_expr;

    return decl;
}

static struct ast_expr *parse_prefix(struct token_stream *ts)
{
    struct ast_expr *expr = NULL;
    struct ast_expr *operand = NULL;
    struct ast_stmt *body = NULL;
    struct ast_type *cast_type = NULL;
    struct osc_token *tok = NULL;
    struct osc_token *next = NULL;

    BUG_ON(!ts, "null token_stream");

    tok = tok_peek(ts, 0);
    if (unlikely(!tok || tok->sym == sym_dump))
        return NULL;

    /* Literals */
    if (tok->sym == sym_numeric_constant || tok->sym == sym_string_literals ||
        tok->sym == sym_true || tok->sym == sym_false) {
        tok = tok_consume(ts);
        expr = ast_new_expr(ts->ctx, EXPR_LITERAL, tok->span);
        expr->literal_sym = tok->symbol;
        expr->literal_type = tok->sym;
        return expr;
    }

    /* Identifiers */
    if (tok->sym == sym_id) {
        tok = tok_consume(ts);
        expr = ast_new_expr(ts->ctx, EXPR_IDENT, tok->span);
        expr->id = tok->symbol;
        return expr;
    }

    /* Unary operators: +, -, *, &, !, ~, ++, -- */
    if (tok->sym == sym_add || tok->sym == sym_minus || tok->sym == sym_aster ||
        tok->sym == sym_bit_and || tok->sym == sym_exclaim ||
        tok->sym == sym_bit_not || tok->sym == sym_inc || tok->sym == sym_dec) {
        tok = tok_consume(ts);
        operand = parse_expr_precedence(ts, PREC_PREFIX);
        expr = ast_new_expr(ts->ctx, EXPR_UNARY, tok->span);
        expr->unary_op = tok->sym;
        expr->operand = operand;
        return expr;
    }

    /* Left parenthesis: Grouped, Cast, or Statement Expression ({ ... }) */
    if (tok->sym == sym_left_paren) {
        next = tok_peek(ts, 1);

        /* GNU Statement Expression: ({ ... }) */
        if (next && next->sym == sym_left_brace) {
            tok = tok_consume(ts); /* consume '(' */
            body = parse_stmt_compound(ts);
            tok_match(ts, sym_right_paren);

            expr = ast_new_expr(ts->ctx, EXPR_STMT_EXPR, tok->span);
            expr->stmt_expr_body = body;
            return expr;
        }

        /* Type cast: (type) expr */
        if (next && is_type_token(next->sym)) {
            tok = tok_consume(ts); /* consume '(' */
            cast_type = parse_type(ts);
            tok_match(ts, sym_right_paren);
            operand = parse_expr_precedence(ts, PREC_PREFIX);

            expr = ast_new_expr(ts->ctx, EXPR_CAST, tok->span);
            expr->cast_type = cast_type;
            expr->cast_operand = operand;
            return expr;
        }

        /* Grouped expression: (expr) */
        tok_consume(ts); /* consume '(' */
        expr = parse_expr(ts);
        tok_match(ts, sym_right_paren);
        return expr;
    }

    return NULL;
}

static struct ast_expr *parse_infix(struct token_stream *ts,
                                    struct ast_expr *left,
                                    enum expr_precedence prec)
{
    struct ast_expr *expr = NULL;
    struct ast_expr *right = NULL;
    struct ast_expr *arg = NULL;
    struct ast_expr *index_expr = NULL;
    struct ast_expr *then_expr = NULL;
    struct ast_expr *else_expr = NULL;
    struct osc_token *tok = NULL;
    struct osc_token *member_tok = NULL;
    enum expr_precedence next_prec = PREC_NONE;
    bool is_right_assoc = false;

    BUG_ON(!ts, "null token_stream");
    BUG_ON(!left, "null left operand");

    tok = tok_peek(ts, 0);
    if (unlikely(!tok || tok->sym == sym_dump))
        return left;

    /* Function call: left(...) */
    if (tok->sym == sym_left_paren) {
        tok = tok_consume(ts);
        expr = ast_new_expr(ts->ctx, EXPR_CALL, tok->span);
        expr->callee = left;

        /* Check for kernel idioms */
        if (left->expr_kind == EXPR_IDENT && left->id) {
            if (strcmp(left->id->name, "container_of") == 0)
                expr->is_container_of = true;
            else if (strcmp(left->id->name, "IS_ERR") == 0 ||
                     strcmp(left->id->name, "PTR_ERR") == 0 ||
                     strcmp(left->id->name, "IS_ERR_OR_NULL") == 0)
                expr->is_err_indicator = true;
        }

        while (1) {
            tok = tok_peek(ts, 0);
            if (!tok || tok->sym == sym_right_paren || tok->sym == sym_dump)
                break;

            arg = parse_expr_precedence(ts, PREC_ASSIGN);
            if (!arg)
                break;

            list_add_tail(&arg->node, &expr->args);
            expr->arg_count++;

            tok = tok_peek(ts, 0);
            if (tok && tok->sym == sym_comma)
                tok_consume(ts);
            else
                break;
        }
        tok_match(ts, sym_right_paren);
        return expr;
    }

    /* Array subscript: left[...] */
    if (tok->sym == sym_left_sq_brace) {
        tok = tok_consume(ts);
        index_expr = parse_expr(ts);
        tok_match(ts, sym_right_sq_brace);

        expr = ast_new_expr(ts->ctx, EXPR_BINARY, tok->span);
        expr->binary_op = sym_left_sq_brace;
        expr->left = left;
        expr->right = index_expr;
        return expr;
    }

    /* Member access: left.member or left->member */
    if (tok->sym == sym_dot || tok->sym == sym_ptr_assign) {
        tok = tok_consume(ts);
        member_tok = tok_peek(ts, 0);
        if (member_tok && member_tok->sym == sym_id)
            tok_consume(ts);

        expr = ast_new_expr(ts->ctx, EXPR_MEMBER, tok->span);
        expr->object = left;
        expr->member = member_tok ? member_tok->symbol : NULL;
        expr->is_arrow = (tok->sym == sym_ptr_assign);
        return expr;
    }

    /* Postfix ++ / -- */
    if (tok->sym == sym_inc || tok->sym == sym_dec) {
        tok = tok_consume(ts);
        expr = ast_new_expr(ts->ctx, EXPR_UNARY, tok->span);
        expr->unary_op = tok->sym;
        expr->operand = left;
        return expr;
    }

    /* Ternary conditional: cond ? then : else */
    if (tok->sym == sym_query) {
        tok = tok_consume(ts);
        then_expr = parse_expr(ts);
        tok_match(ts, sym_colon);
        else_expr = parse_expr_precedence(ts, PREC_TERNARY - 1);

        expr = ast_new_expr(ts->ctx, EXPR_TERNARY, tok->span);
        expr->cond = left;
        expr->then_expr = then_expr;
        expr->else_expr = else_expr;
        return expr;
    }

    /* Assignment: left = right */
    if (tok->sym == sym_eq) {
        tok = tok_consume(ts);
        right = parse_expr_precedence(ts, PREC_ASSIGN - 1);

        expr = ast_new_expr(ts->ctx, EXPR_ASSIGN, tok->span);
        expr->binary_op = sym_eq;
        expr->left = left;
        expr->right = right;
        return expr;
    }

    /* Binary operators (+, -, *, /, ==, !=, <, >, etc.) */
    tok = tok_consume(ts);
    is_right_assoc = (prec == PREC_ASSIGN || prec == PREC_TERNARY);
    next_prec = is_right_assoc ? prec - 1 : prec;
    right = parse_expr_precedence(ts, next_prec);

    expr = ast_new_expr(ts->ctx, EXPR_BINARY, tok->span);
    expr->binary_op = tok->sym;
    expr->left = left;
    expr->right = right;

    return expr;
}

struct ast_expr *parse_expr_precedence(struct token_stream *ts,
                                       enum expr_precedence min_prec)
{
    struct ast_expr *left = NULL;
    struct osc_token *tok = NULL;
    enum expr_precedence prec = PREC_NONE;

    if (unlikely(!ts))
        return NULL;

    left = parse_prefix(ts);
    if (!left)
        return NULL;

    while (1) {
        tok = tok_peek(ts, 0);
        if (!tok || tok->sym == sym_dump || tok->sym == sym_seq_point)
            break;

        prec = get_infix_precedence(tok->sym);
        if (prec <= min_prec)
            break;

        left = parse_infix(ts, left, prec);
        if (!left)
            break;
    }

    return left;
}

struct ast_expr *parse_expr(struct token_stream *ts)
{
    return parse_expr_precedence(ts, PREC_NONE);
}

struct parse_scope_entry {
    struct ast_stmt *compound;
    struct parse_scope_entry *prev;
};

static struct ast_stmt *parse_asm_stmt(struct token_stream *ts)
{
    struct ast_stmt *stmt = NULL;
    struct osc_token *tok = NULL;
    struct osc_span span = { 0 };
    const char *asm_code = NULL;
    int paren_depth = 0;

    BUG_ON(!ts, "null token_stream");

    tok = tok_consume(ts); /* consume asm or __asm__ */
    if (tok)
        span = tok->span;

    /* Skip optional qualifiers: volatile, __volatile__, inline, goto */
    while (1) {
        tok = tok_peek(ts, 0);
        if (!tok || tok->sym == sym_dump)
            break;
        if (tok->sym == sym_volatile || tok->sym == sym_inline ||
            tok->sym == sym_goto ||
            (tok->symbol && strcmp(tok->symbol->name, "__volatile__") == 0)) {
            tok_consume(ts);
        } else {
            break;
        }
    }

    /* Expect opening '(' */
    if (!tok_match(ts, sym_left_paren))
        return NULL;

    paren_depth = 1;

    /* First token inside might be the assembly string literal */
    tok = tok_peek(ts, 0);
    if (tok && tok->sym == sym_string_literals && tok->symbol)
        asm_code = tok->symbol->name;

    /* Skip until balanced closing ')' */
    while (paren_depth > 0) {
        tok = tok_consume(ts);
        if (!tok || tok->sym == sym_dump)
            break;
        if (tok->sym == sym_left_paren)
            paren_depth++;
        else if (tok->sym == sym_right_paren)
            paren_depth--;
    }

    tok_match(ts, sym_seq_point);

    stmt = ast_new_stmt(ts->ctx, STMT_ASM, span);
    stmt->asm_code = asm_code;

    return stmt;
}

static struct ast_stmt *parse_static_assert_stmt(struct token_stream *ts)
{
    struct ast_stmt *stmt = NULL;
    struct ast_expr *cond = NULL;
    struct osc_token *tok = NULL;
    struct osc_token *msg_tok = NULL;
    struct osc_span span = { 0 };
    const char *msg = NULL;

    BUG_ON(!ts, "null token_stream");

    tok = tok_consume(ts); /* consume _Static_assert */
    if (tok)
        span = tok->span;

    if (!tok_match(ts, sym_left_paren))
        return NULL;

    cond = parse_expr_precedence(ts, PREC_ASSIGN);

    if (tok_match(ts, sym_comma)) {
        msg_tok = tok_peek(ts, 0);
        if (msg_tok && msg_tok->sym == sym_string_literals && msg_tok->symbol) {
            msg = msg_tok->symbol->name;
            tok_consume(ts);
        }
    }

    tok_match(ts, sym_right_paren);
    tok_match(ts, sym_seq_point);

    stmt = ast_new_stmt(ts->ctx, STMT_STATIC_ASSERT, span);
    stmt->assert_cond = cond;
    stmt->assert_msg = msg;

    return stmt;
}

struct ast_stmt *parse_stmt_compound(struct token_stream *ts)
{
    struct parse_scope_entry *scope_stack = NULL;
    struct parse_scope_entry *new_entry = NULL;
    struct ast_stmt *root_compound = NULL;
    struct ast_stmt *stmt = NULL;
    struct osc_token *tok = NULL;
    struct osc_span span = { 0 };

    BUG_ON(!ts, "null token_stream");

    tok = tok_peek(ts, 0);
    if (tok)
        span = tok->span;

    if (!tok_match(ts, sym_left_brace))
        return NULL;

    root_compound = ast_new_stmt(ts->ctx, STMT_COMPOUND, span);
    scope_stack = osc_arena_zalloc(&ts->ctx->arena, sizeof(*scope_stack));
    BUG_ON(!scope_stack, "arena alloc failed for scope_stack");
    scope_stack->compound = root_compound;
    scope_stack->prev = NULL;

    while (scope_stack) {
        tok = tok_peek(ts, 0);
        if (!tok || tok->sym == sym_dump)
            break;

        /* Exiting compound statement '}' */
        if (tok->sym == sym_right_brace) {
            tok_consume(ts);
            if (scope_stack->prev) {
                list_add_tail(&scope_stack->compound->node,
                              &scope_stack->prev->compound->stmts);
                scope_stack = scope_stack->prev;
                continue;
            } else {
                scope_stack = NULL;
                break;
            }
        }

        /* Nested compound statement '{' */
        if (tok->sym == sym_left_brace) {
            tok = tok_consume(ts);
            new_entry = osc_arena_zalloc(&ts->ctx->arena, sizeof(*new_entry));
            BUG_ON(!new_entry, "arena alloc failed for new_entry");
            new_entry->compound =
                ast_new_stmt(ts->ctx, STMT_COMPOUND, tok->span);
            new_entry->prev = scope_stack;
            scope_stack = new_entry;
            continue;
        }

        /* Parse statement or declaration */
        stmt = parse_stmt(ts);
        if (stmt) {
            list_add_tail(&stmt->node, &scope_stack->compound->stmts);
            if (stmt->stmt_kind == STMT_DECL && stmt->stmt_decl)
                list_add_tail(&stmt->stmt_decl->node,
                              &scope_stack->compound->decls);
        }
    }

    return root_compound;
}

struct ast_stmt *parse_stmt(struct token_stream *ts)
{
    struct ast_stmt *stmt = NULL;
    struct ast_stmt *then_stmt = NULL;
    struct ast_stmt *else_stmt = NULL;
    struct ast_stmt *body = NULL;
    struct ast_stmt *init_stmt = NULL;
    struct ast_decl *decl = NULL;
    struct ast_expr *cond = NULL;
    struct ast_expr *init_expr = NULL;
    struct ast_expr *step_expr = NULL;
    struct ast_expr *val_expr = NULL;
    struct ast_expr *ret_expr = NULL;
    struct osc_token *tok = NULL;
    struct osc_token *next = NULL;
    struct osc_token *label_tok = NULL;
    struct osc_span span = { 0 };

    BUG_ON(!ts, "null token_stream");

    tok = tok_peek(ts, 0);
    if (!tok || tok->sym == sym_dump)
        return NULL;

    /* Compound statement '{ ... }' */
    if (tok->sym == sym_left_brace)
        return parse_stmt_compound(ts);

    /* if statement */
    if (tok->sym == sym_if) {
        tok = tok_consume(ts);
        span = tok->span;
        tok_match(ts, sym_left_paren);
        cond = parse_expr(ts);
        tok_match(ts, sym_right_paren);
        then_stmt = parse_stmt(ts);
        if (tok_match(ts, sym_else))
            else_stmt = parse_stmt(ts);

        stmt = ast_new_stmt(ts->ctx, STMT_IF, span);
        stmt->if_cond = cond;
        stmt->then_stmt = then_stmt;
        stmt->else_stmt = else_stmt;
        return stmt;
    }

    /* while loop */
    if (tok->sym == sym_while) {
        tok = tok_consume(ts);
        span = tok->span;
        tok_match(ts, sym_left_paren);
        cond = parse_expr(ts);
        tok_match(ts, sym_right_paren);
        body = parse_stmt(ts);

        stmt = ast_new_stmt(ts->ctx, STMT_WHILE, span);
        stmt->loop_cond = cond;
        stmt->loop_body = body;
        return stmt;
    }

    /* do-while loop */
    if (tok->sym == sym_do) {
        tok = tok_consume(ts);
        span = tok->span;
        body = parse_stmt(ts);
        tok_match(ts, sym_while);
        tok_match(ts, sym_left_paren);
        cond = parse_expr(ts);
        tok_match(ts, sym_right_paren);
        tok_match(ts, sym_seq_point);

        stmt = ast_new_stmt(ts->ctx, STMT_DO_WHILE, span);
        stmt->loop_cond = cond;
        stmt->loop_body = body;
        return stmt;
    }

    /* for loop */
    if (tok->sym == sym_for) {
        tok = tok_consume(ts);
        span = tok->span;
        tok_match(ts, sym_left_paren);

        if (tok_peek(ts, 0) && is_type_token(tok_peek(ts, 0)->sym)) {
            decl = parse_decl(ts);
            tok_match(ts, sym_seq_point);
            init_stmt = ast_new_stmt(ts->ctx, STMT_DECL, decl->span);
            init_stmt->stmt_decl = decl;
        } else if (tok_peek(ts, 0) && tok_peek(ts, 0)->sym != sym_seq_point) {
            init_expr = parse_expr(ts);
            tok_match(ts, sym_seq_point);
        } else {
            tok_match(ts, sym_seq_point);
        }

        if (tok_peek(ts, 0) && tok_peek(ts, 0)->sym != sym_seq_point)
            cond = parse_expr(ts);
        tok_match(ts, sym_seq_point);

        if (tok_peek(ts, 0) && tok_peek(ts, 0)->sym != sym_right_paren)
            step_expr = parse_expr(ts);
        tok_match(ts, sym_right_paren);

        body = parse_stmt(ts);

        stmt = ast_new_stmt(ts->ctx, STMT_FOR, span);
        stmt->for_init_stmt = init_stmt;
        stmt->for_init_expr = init_expr;
        stmt->for_cond = cond;
        stmt->for_step = step_expr;
        stmt->for_body = body;
        return stmt;
    }

    /* switch statement */
    if (tok->sym == sym_switch) {
        tok = tok_consume(ts);
        span = tok->span;
        tok_match(ts, sym_left_paren);
        cond = parse_expr(ts);
        tok_match(ts, sym_right_paren);
        body = parse_stmt(ts);

        stmt = ast_new_stmt(ts->ctx, STMT_SWITCH, span);
        stmt->switch_cond = cond;
        stmt->switch_body = body;
        return stmt;
    }

    /* case label */
    if (tok->sym == sym_case) {
        tok = tok_consume(ts);
        span = tok->span;
        val_expr = parse_expr_precedence(ts, PREC_ASSIGN);
        tok_match(ts, sym_colon);
        if (tok_peek(ts, 0) && tok_peek(ts, 0)->sym != sym_case &&
            tok_peek(ts, 0)->sym != sym_default &&
            tok_peek(ts, 0)->sym != sym_right_brace)
            body = parse_stmt(ts);

        stmt = ast_new_stmt(ts->ctx, STMT_CASE, span);
        stmt->case_val = val_expr;
        stmt->case_body = body;
        return stmt;
    }

    /* default label */
    if (tok->sym == sym_default) {
        tok = tok_consume(ts);
        span = tok->span;
        tok_match(ts, sym_colon);
        if (tok_peek(ts, 0) && tok_peek(ts, 0)->sym != sym_case &&
            tok_peek(ts, 0)->sym != sym_default &&
            tok_peek(ts, 0)->sym != sym_right_brace)
            body = parse_stmt(ts);

        stmt = ast_new_stmt(ts->ctx, STMT_DEFAULT, span);
        stmt->case_body = body;
        return stmt;
    }

    /* goto statement */
    if (tok->sym == sym_goto) {
        tok = tok_consume(ts);
        span = tok->span;
        label_tok = tok_peek(ts, 0);
        if (label_tok && label_tok->sym == sym_id)
            tok_consume(ts);
        tok_match(ts, sym_seq_point);

        stmt = ast_new_stmt(ts->ctx, STMT_GOTO, span);
        stmt->label_name = label_tok ? label_tok->symbol : NULL;
        return stmt;
    }

    /* break statement */
    if (tok->sym == sym_break) {
        tok = tok_consume(ts);
        tok_match(ts, sym_seq_point);
        return ast_new_stmt(ts->ctx, STMT_BREAK, tok->span);
    }

    /* continue statement */
    if (tok->sym == sym_continue) {
        tok = tok_consume(ts);
        tok_match(ts, sym_seq_point);
        return ast_new_stmt(ts->ctx, STMT_CONTINUE, tok->span);
    }

    /* return statement */
    if (tok->sym == sym_return) {
        tok = tok_consume(ts);
        span = tok->span;
        if (tok_peek(ts, 0) && tok_peek(ts, 0)->sym != sym_seq_point)
            ret_expr = parse_expr(ts);
        tok_match(ts, sym_seq_point);

        stmt = ast_new_stmt(ts->ctx, STMT_RETURN, span);
        stmt->ret_expr = ret_expr;
        return stmt;
    }

    /* Inline assembly statement */
    if (tok->sym == sym_asm || tok->sym == sym___asm__)
        return parse_asm_stmt(ts);

    /* C11 _Static_assert statement */
    if (tok->sym == sym__Static_assert)
        return parse_static_assert_stmt(ts);

    /* Labeled statement (id followed by ':') */
    if (tok->sym == sym_id) {
        next = tok_peek(ts, 1);
        if (next && next->sym == sym_colon) {
            label_tok = tok_consume(ts);
            tok_consume(ts); /* consume ':' */
            if (tok_peek(ts, 0) && tok_peek(ts, 0)->sym != sym_right_brace)
                body = parse_stmt(ts);

            stmt = ast_new_stmt(ts->ctx, STMT_LABEL, label_tok->span);
            stmt->label_name = label_tok->symbol;
            stmt->label_stmt = body;
            return stmt;
        }
    }

    /* Empty statement ';' */
    if (tok->sym == sym_seq_point) {
        tok = tok_consume(ts);
        return ast_new_stmt(ts->ctx, STMT_EMPTY, tok->span);
    }

    /* Variable declaration statement */
    if (is_type_token(tok->sym)) {
        decl = parse_decl(ts);
        tok_match(ts, sym_seq_point);

        stmt = ast_new_stmt(ts->ctx, STMT_DECL, decl->span);
        stmt->stmt_decl = decl;
        return stmt;
    }

    /* Expression statement */
    init_expr = parse_expr(ts);
    tok_match(ts, sym_seq_point);
    if (init_expr) {
        stmt = ast_new_stmt(ts->ctx, STMT_EXPR, init_expr->span);
        stmt->expr = init_expr;
    }

    return stmt;
}
