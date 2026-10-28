#ifndef __OSC_AST_H__
#define __OSC_AST_H__

#include <osc/compiler.h>
#include <osc/list.h>
#include <osc/token.h>
#include <stdbool.h>
#include <stddef.h>

struct osc_context;

/* Base AST node kinds */
enum ast_node_kind {
    AST_NODE_UNKNOWN = 0,
    AST_NODE_TYPE,
    AST_NODE_DECL,
    AST_NODE_STMT,
    AST_NODE_EXPR,
};

/* Expression kinds */
enum ast_expr_kind {
    EXPR_UNKNOWN = 0,
    EXPR_LITERAL,
    EXPR_IDENT,
    EXPR_UNARY,
    EXPR_BINARY,
    EXPR_TERNARY,
    EXPR_ASSIGN,
    EXPR_CALL,
    EXPR_CAST,
    EXPR_MEMBER,
    EXPR_COMPOUND_LITERAL,
    EXPR_GENERIC,
    EXPR_STMT_EXPR,
};

/* Statement kinds */
enum ast_stmt_kind {
    STMT_UNKNOWN = 0,
    STMT_EMPTY,
    STMT_EXPR,
    STMT_COMPOUND,
    STMT_IF,
    STMT_SWITCH,
    STMT_CASE,
    STMT_DEFAULT,
    STMT_WHILE,
    STMT_DO_WHILE,
    STMT_FOR,
    STMT_GOTO,
    STMT_LABEL,
    STMT_CONTINUE,
    STMT_BREAK,
    STMT_RETURN,
    STMT_DECL,
    STMT_ASM,
    STMT_STATIC_ASSERT,
};

/* Type qualifiers and specifiers */
#define TYPE_QUAL_CONST (1 << 0)
#define TYPE_QUAL_VOLATILE (1 << 1)
#define TYPE_QUAL_RESTRICT (1 << 2)
#define TYPE_QUAL_ATOMIC (1 << 3)

/* AST Type representation */
struct ast_type {
    enum ast_node_kind kind;
    struct osc_span span;

    int base_type; /* sym_int, sym_char, sym_struct, etc. */
    unsigned int qualifiers; /* TYPE_QUAL_* */
    unsigned int osc_attrs; /* ATTR_FLAGS_MUT, BRW, CLONE */
    unsigned int ptr_depth; /* 0 for value, 1 for *, 2 for **, etc. */

    struct symbol *tag; /* struct or union tag name */
    bool is_anonymous;
    struct list_head members; /* Member declarations (struct ast_decl) */

    bool is_function;
    struct ast_type *return_type; /* For function types */
    struct list_head params; /* Parameter declarations */

    bool is_array;
    size_t array_elements;
};

/* AST Declaration */
struct ast_decl {
    enum ast_node_kind kind;
    struct osc_span span;

    int storage_class; /* sym_auto, sym_static, sym__Thread_local, etc. */
    bool is_inline;
    bool is_noreturn;

    struct ast_type *type;
    struct symbol *id;
    struct ast_expr *init;

    /* GNU __attribute__((__cleanup__(func))) */
    struct symbol *cleanup_fn;

    /* Function definition body */
    struct ast_stmt *body;

    struct list_head node; /* Link in declaration lists */
};

/* C11 _Generic association */
struct ast_generic_assoc {
    struct ast_type *type; /* NULL for default */
    struct ast_expr *expr;
    struct list_head node;
};

/* AST Expression */
struct ast_expr {
    enum ast_node_kind kind;
    enum ast_expr_kind expr_kind;
    struct osc_span span;

    struct ast_type *type;
    bool is_lvalue;

    /* Kernel semantic tags */
    bool is_container_of;
    bool is_err_indicator;

    union {
        /* EXPR_LITERAL */
        struct {
            struct symbol *literal_sym;
            int literal_type;
        };

        /* EXPR_IDENT */
        struct {
            struct symbol *id;
            struct ast_decl *decl_ref;
        };

        /* EXPR_UNARY */
        struct {
            int unary_op;
            struct ast_expr *operand;
        };

        /* EXPR_BINARY & EXPR_ASSIGN */
        struct {
            int binary_op;
            struct ast_expr *left;
            struct ast_expr *right;
        };

        /* EXPR_TERNARY (cond ? then : else) */
        struct {
            struct ast_expr *cond;
            struct ast_expr *then_expr;
            struct ast_expr *else_expr;
        };

        /* EXPR_CALL */
        struct {
            struct ast_expr *callee;
            struct list_head args; /* List of struct ast_expr via node */
            size_t arg_count;
        };

        /* EXPR_MEMBER (obj.field or obj->field) */
        struct {
            struct ast_expr *object;
            struct symbol *member;
            bool is_arrow; /* true for ->, false for . */
        };

        /* EXPR_CAST */
        struct {
            struct ast_type *cast_type;
            struct ast_expr *cast_operand;
        };

        /* EXPR_STMT_EXPR ({ ... }) */
        struct {
            struct ast_stmt *stmt_expr_body;
        };

        /* EXPR_GENERIC */
        struct {
            struct ast_expr *generic_control;
            struct list_head
                generic_assoc; /* List of struct ast_generic_assoc */
        };

        /* EXPR_COMPOUND_LITERAL */
        struct {
            struct ast_type *compound_literal_type;
            struct list_head compound_literal_inits;
        };
    };

    struct list_head node; /* Link in argument / expression lists */
};

/* AST Statement */
struct ast_stmt {
    enum ast_node_kind kind;
    enum ast_stmt_kind stmt_kind;
    struct osc_span span;

    union {
        /* STMT_EXPR */
        struct {
            struct ast_expr *expr;
        };

        /* STMT_COMPOUND { ... } */
        struct {
            struct list_head stmts; /* List of struct ast_stmt */
            struct list_head decls; /* List of struct ast_decl declared */
        };

        /* STMT_IF */
        struct {
            struct ast_expr *if_cond;
            struct ast_stmt *then_stmt;
            struct ast_stmt *else_stmt;
        };

        /* STMT_SWITCH */
        struct {
            struct ast_expr *switch_cond;
            struct ast_stmt *switch_body;
        };

        /* STMT_CASE & STMT_DEFAULT */
        struct {
            struct ast_expr *case_val;
            struct ast_stmt *case_body;
        };

        /* STMT_WHILE & STMT_DO_WHILE */
        struct {
            struct ast_expr *loop_cond;
            struct ast_stmt *loop_body;
        };

        /* STMT_FOR */
        struct {
            struct ast_stmt *for_init_stmt;
            struct ast_expr *for_init_expr;
            struct ast_expr *for_cond;
            struct ast_expr *for_step;
            struct ast_stmt *for_body;
        };

        /* STMT_GOTO & STMT_LABEL */
        struct {
            struct symbol *label_name;
            struct ast_stmt *label_stmt;
        };

        /* STMT_RETURN */
        struct {
            struct ast_expr *ret_expr;
        };

        /* STMT_DECL */
        struct {
            struct ast_decl *stmt_decl;
        };

        /* STMT_ASM */
        struct {
            const char *asm_code;
        };

        /* STMT_STATIC_ASSERT */
        struct {
            struct ast_expr *assert_cond;
            const char *assert_msg;
        };
    };

    struct list_head node; /* Link in statement lists */
};

/* Typedef entry */
struct ast_typedef_entry {
    struct symbol *name;
    struct ast_type *type;
    struct list_head node;
};

/* Translation unit */
struct ast_translation_unit {
    struct list_head decls; /* List of struct ast_decl */
    struct list_head typedefs; /* List of struct ast_typedef_entry */
    struct osc_context *ctx;
};

/* AST Allocation Helpers */
struct ast_type *ast_new_type(struct osc_context *ctx, int base_type,
                              struct osc_span span);
struct ast_decl *ast_new_decl(struct osc_context *ctx, struct ast_type *type,
                              struct symbol *id, struct osc_span span);
struct ast_expr *ast_new_expr(struct osc_context *ctx, enum ast_expr_kind kind,
                              struct osc_span span);
struct ast_stmt *ast_new_stmt(struct osc_context *ctx, enum ast_stmt_kind kind,
                              struct osc_span span);
struct ast_translation_unit *ast_new_translation_unit(struct osc_context *ctx);

/* Operator Precedence levels for Pratt parser */
enum expr_precedence {
    PREC_NONE = 0,
    PREC_COMMA, /* , */
    PREC_ASSIGN, /* = += -= etc. (right-associative) */
    PREC_TERNARY, /* ? : (right-associative) */
    PREC_LOGICAL_OR, /* || */
    PREC_LOGICAL_AND, /* && */
    PREC_BIT_OR, /* | */
    PREC_BIT_XOR, /* ^ */
    PREC_BIT_AND, /* & */
    PREC_EQUALITY, /* == != */
    PREC_RELATIONAL, /* < <= > >= */
    PREC_SHIFT, /* << >> */
    PREC_ADDITIVE, /* + - */
    PREC_MULTIPLICATIVE, /* * / % */
    PREC_PREFIX, /* ++ -- + - ! ~ * & sizeof _Alignof */
    PREC_POSTFIX, /* () [] . -> */
    PREC_PRIMARY,
};

/* Pratt Expression Parser */
struct ast_expr *parse_expr(struct token_stream *ts);
struct ast_expr *parse_expr_precedence(struct token_stream *ts,
                                       enum expr_precedence min_prec);

/* Statement and Declaration Parsers */
struct ast_stmt *parse_stmt(struct token_stream *ts);
struct ast_stmt *parse_stmt_compound(struct token_stream *ts);
struct ast_decl *parse_decl(struct token_stream *ts);
struct list_head *parse_decl_list(struct token_stream *ts,
                                  struct list_head *out_list);
struct ast_type *parse_type(struct token_stream *ts);
struct ast_translation_unit *parse_translation_unit(struct token_stream *ts);

/* AST Pretty Printer and Debug Dumpers */
void ast_dump_type(struct ast_type *type, FILE *out);
void ast_dump_expr(struct ast_expr *expr, FILE *out, int indent);
void ast_dump_stmt(struct ast_stmt *stmt, FILE *out, int indent);
void ast_dump_decl(struct ast_decl *decl, FILE *out, int indent);
void ast_dump_translation_unit(struct ast_translation_unit *tu, FILE *out);

#endif /* __OSC_AST_H__ */
