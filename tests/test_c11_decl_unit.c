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
    struct list_head decl_list;
    struct ast_decl *d1 = NULL;
    struct ast_decl *d2 = NULL;
    struct ast_decl *d3 = NULL;
    struct ast_translation_unit *tu = NULL;
    struct ast_expr *expr = NULL;
    struct ast_generic_assoc *assoc = NULL;

    memset(&fi, 0, sizeof(fi));
    strcpy(fi.name, "test_decl.c");

    osc_context_init(&ctx, &fi, OSC_MODE_AUTO);

    /* 1. Multi-declarator comma-separated list: int a, *b = 0, c[10]; */
    tokenize_snippet(&ctx, &ts, "int a, *b = 0, c[10];\n");
    list_init(&decl_list);
    parse_decl_list(&ts, &decl_list);

    assert(!list_empty(&decl_list));
    d1 = list_first_entry(&decl_list, struct ast_decl, node);
    assert(d1 != NULL && strcmp(d1->id->name, "a") == 0);
    assert(d1->type->ptr_depth == 0);
    assert(d1->type->is_array == false);

    d2 = list_next_entry(d1, node);
    assert(d2 != NULL && strcmp(d2->id->name, "b") == 0);
    assert(d2->type->ptr_depth == 1);
    assert(d2->init != NULL);

    d3 = list_next_entry(d2, node);
    assert(d3 != NULL && strcmp(d3->id->name, "c") == 0);
    assert(d3->type->is_array == true);
    assert(d3->type->array_elements == 10);

    /* 2. Anonymous Structs and Anonymous Unions */
    tokenize_snippet(&ctx, &ts, "struct {\n"
                                "    int x;\n"
                                "    union {\n"
                                "        int y;\n"
                                "        char z;\n"
                                "    };\n"
                                "} my_struct;\n");
    list_init(&decl_list);
    parse_decl_list(&ts, &decl_list);
    assert(!list_empty(&decl_list));
    d1 = list_first_entry(&decl_list, struct ast_decl, node);
    assert(d1 != NULL && strcmp(d1->id->name, "my_struct") == 0);
    assert(d1->type->base_type == sym_struct);
    assert(d1->type->is_anonymous == true);
    assert(!list_empty(&d1->type->members));

    /* 3. C11 _Atomic Type Qualifiers and Specifiers */
    tokenize_snippet(&ctx, &ts, "_Atomic int atomic_val;\n");
    d1 = parse_decl(&ts);
    assert(d1 != NULL);
    assert(d1->type->qualifiers & TYPE_QUAL_ATOMIC);
    assert(strcmp(d1->id->name, "atomic_val") == 0);

    tokenize_snippet(&ctx, &ts, "_Atomic(int *) atomic_ptr;\n");
    d1 = parse_decl(&ts);
    assert(d1 != NULL);
    assert(d1->type->qualifiers & TYPE_QUAL_ATOMIC);
    assert(d1->type->ptr_depth == 1);
    assert(strcmp(d1->id->name, "atomic_ptr") == 0);

    /* 4. C11 _Generic Selection Expression */
    tokenize_snippet(&ctx, &ts, "_Generic(val, int: 1, default: 0);");
    expr = parse_expr(&ts);
    assert(expr != NULL);
    assert(expr->expr_kind == EXPR_GENERIC);
    assert(expr->generic_control != NULL);
    assert(!list_empty(&expr->generic_assoc));

    assoc = list_first_entry(&expr->generic_assoc, struct ast_generic_assoc, node);
    assert(assoc != NULL);
    assert(assoc->type != NULL && assoc->type->base_type == sym_int);
    assert(assoc->expr != NULL);

    assoc = list_last_entry(&expr->generic_assoc, struct ast_generic_assoc, node);
    assert(assoc != NULL);
    assert(assoc->type == NULL); /* default association */

    /* 5. Complete Translation Unit with Function Definitions & Prototypes */
    tokenize_snippet(&ctx, &ts, "int add(int a, int b) {\n"
                                "    return a + b;\n"
                                "}\n"
                                "void noop(void);\n"
                                "static int global_val = 100;\n");
    tu = parse_translation_unit(&ts);
    assert(tu != NULL);
    assert(!list_empty(&tu->decls));

    /* Function definition: add */
    d1 = list_first_entry(&tu->decls, struct ast_decl, node);
    assert(d1 != NULL && strcmp(d1->id->name, "add") == 0);
    assert(d1->type->is_function == true);
    assert(d1->body != NULL);
    assert(d1->body->stmt_kind == STMT_COMPOUND);
    assert(!list_empty(&d1->type->params));

    /* Function prototype: noop */
    d2 = list_next_entry(d1, node);
    assert(d2 != NULL && strcmp(d2->id->name, "noop") == 0);
    assert(d2->type->is_function == true);
    assert(d2->body == NULL);
    assert(list_empty(&d2->type->params));

    /* Global variable */
    d3 = list_next_entry(d2, node);
    assert(d3 != NULL && strcmp(d3->id->name, "global_val") == 0);
    assert(d3->storage_class == sym_static);
    assert(d3->init != NULL);

    osc_context_destroy(&ctx);
    symbol_id_container_release();

    printf("C11 declaration and translation unit tests passed!\n");
    return 0;
}
