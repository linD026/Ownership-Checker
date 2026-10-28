#include <osc/parser.h>
#include <osc/check_list.h>
#include <osc/compiler.h>
#include <osc/debug.h>
#include <osc/ast.h>

__allow_unused void __debug_token(struct scan_file_control *sfc, int sym,
                                  struct symbol *symbol)
{
#ifdef CONFIG_DEBUG
    if (symbol == NULL) {
        int __c = debug_sym_one_char(sym);
        if (__c != -ENODATA)
            print("char          : |%c|\n", (char)__c);
    } else
        print("symbol(%2d, %2u): |%s|\n", symbol->flags, symbol->len,
              symbol->name);
#endif /* CONFIG_DEBUG */
}

static __allow_unused void raw_debug_object(struct object *obj)
{
    if (obj->storage_class != sym_dump)
        print("%s ", token_name(obj->storage_class));
    if (obj->type != sym_dump) {
        print("%s ", token_name(obj->type));
        if (obj->type == sym_struct)
            print("%s ", obj->struct_id->name);
    }
    if (obj->attr & ATTR_FLAS_MASK) {
        if (obj->attr & ATTR_FLAGS_BRW)
            print("__brw ");
        if (obj->attr & ATTR_FLAGS_CLONE)
            print("__clone ");
        if (obj->attr & ATTR_FLAGS_MUT)
            print("__mut ");
    }
    if (obj->is_ptr)
        print("*");
    if (obj->id)
        print("%s", obj->id->name);
}

void debug_object(struct object *obj, const char *note)
{
#ifdef CONFIG_DEBUG
    print("[OBJECT] %s: ", note);
    raw_debug_object(obj);
    print(" \n");
#endif /* CONFIG_DEBUG */
}

void debug_variable(struct variable *var, const char *note)
{
#ifdef CONFIG_DEBUG
    struct ptr_info *info = &var->ptr_info;
    print("[VAR] ");
    debug_object(&var->object, note);
    if (info->flags & PTR_INFO_FUNC_ARG) {
        print("[VAR] is func parrameter\n");
    }
    if (info->flags & PTR_INFO_SET) {
        print("[VAR] set at:%ld:%u\n", info->set_info.line,
              info->set_info.offset);
    }
    if (info->flags & PTR_INFO_DROPPED) {
        print("[VAR] dropped at:%ld:%u\n", info->dropped_info.line,
              info->dropped_info.offset);
    }
#endif
}

static __allow_unused void debug_space_level(int nested_level)
{
    for (int i = 0; i < nested_level; i++)
        print("    ");
}

static __allow_unused void raw_debug_structure(struct structure *structure,
                                               int nested_level)
{
    print("struct %s ", structure->object.struct_id->name);
    print("{\n");
    list_for_each (&structure->struct_head) {
        struct variable *mem = container_of(curr, struct variable, struct_node);
        debug_space_level(nested_level);
        if (mem->object.type == sym_struct)
            raw_debug_structure(&mem->struct_info, nested_level + 1);
        else {
            raw_debug_object(&mem->object);
            print(";\n");
        }
    }

    if (nested_level > 1)
        debug_space_level(nested_level - 1);

    if (structure->object.id) {
        print("} %s;\n", structure->object.id->name);
    } else {
        print("};\n");
    }
}

void debug_structure(struct structure *structure, const char *note)
{
#ifdef CONFIG_DEBUG
    print("[STRUCT START]: %s\n", note);
    raw_debug_structure(structure, 1);
    print("[STRUCT END]\n");
#endif /* CONFIG_DEBUG */
}

void debug_function(struct function *function)
{
#ifdef CONFIG_DEBUG
    print("[FUNC] ");
    raw_debug_object(&function->object);
    print(" (");
    if (unlikely(list_empty(&function->parameter_head)))
        print("void");
    else {
        list_for_each (&function->parameter_head) {
            struct variable *param =
                container_of(curr, struct variable, parameter_node);
            raw_debug_object(&param->object);
            if (curr->next != &function->parameter_head)
                print(", ");
        }
    }
    print(")");

    // TODO: scope object
    print("\n");
#endif /* CONFIG_DEBUG */
}

static void ast_dump_indent(FILE *out, int indent)
{
    int i = 0;

    for (i = 0; i < indent; i++)
        fprintf(out, "    ");
}

void ast_dump_type(struct ast_type *type, FILE *out)
{
    struct ast_decl *member = NULL;
    struct ast_decl *param = NULL;
    unsigned int i = 0;
    bool first = true;

    if (!type || !out)
        return;

    if (type->qualifiers & TYPE_QUAL_CONST)
        fprintf(out, "const ");
    if (type->qualifiers & TYPE_QUAL_ATOMIC)
        fprintf(out, "_Atomic ");
    if (type->osc_attrs & ATTR_FLAGS_MUT)
        fprintf(out, "__mut ");
    if (type->osc_attrs & ATTR_FLAGS_BRW)
        fprintf(out, "__brw ");
    if (type->osc_attrs & ATTR_FLAGS_CLONE)
        fprintf(out, "__clone ");

    if (type->base_type == sym_struct) {
        fprintf(out, "struct ");
        if (type->tag && !type->is_anonymous)
            fprintf(out, "%s ", type->tag->name);
        if (!list_empty(&type->members)) {
            fprintf(out, "{\n");
            list_for_each_entry (member, &type->members, node) {
                ast_dump_indent(out, 1);
                ast_dump_decl(member, out, 1);
                fprintf(out, ";\n");
            }
            fprintf(out, "} ");
        }
    } else if (type->base_type == sym_union) {
        fprintf(out, "union ");
        if (type->tag && !type->is_anonymous)
            fprintf(out, "%s ", type->tag->name);
        if (!list_empty(&type->members)) {
            fprintf(out, "{\n");
            list_for_each_entry (member, &type->members, node) {
                ast_dump_indent(out, 1);
                ast_dump_decl(member, out, 1);
                fprintf(out, ";\n");
            }
            fprintf(out, "} ");
        }
    } else {
        fprintf(out, "%s ", token_name(type->base_type));
    }

    for (i = 0; i < type->ptr_depth; i++)
        fprintf(out, "*");

    if (type->is_array)
        fprintf(out, "[%zu]", type->array_elements);

    if (type->is_function) {
        fprintf(out, "(");
        if (list_empty(&type->params)) {
            fprintf(out, "void");
        } else {
            first = true;
            list_for_each_entry (param, &type->params, node) {
                if (!first)
                    fprintf(out, ", ");
                ast_dump_decl(param, out, 0);
                first = false;
            }
        }
        fprintf(out, ")");
    }
}

void ast_dump_expr(struct ast_expr *expr, FILE *out, int indent)
{
    struct ast_generic_assoc *assoc = NULL;
    struct ast_expr *arg = NULL;
    bool first = true;

    if (!expr || !out)
        return;

    switch (expr->expr_kind) {
    case EXPR_LITERAL:
        if (expr->literal_sym)
            fprintf(out, "%s", expr->literal_sym->name);
        else
            fprintf(out, "<literal>");
        break;
    case EXPR_IDENT:
        if (expr->id)
            fprintf(out, "%s", expr->id->name);
        else
            fprintf(out, "<ident>");
        break;
    case EXPR_UNARY:
        fprintf(out, "(%s", token_name(expr->unary_op));
        ast_dump_expr(expr->operand, out, 0);
        fprintf(out, ")");
        break;
    case EXPR_BINARY:
        if (expr->binary_op == sym_left_sq_brace) {
            ast_dump_expr(expr->left, out, 0);
            fprintf(out, "[");
            ast_dump_expr(expr->right, out, 0);
            fprintf(out, "]");
        } else {
            fprintf(out, "(");
            ast_dump_expr(expr->left, out, 0);
            fprintf(out, " %s ", token_name(expr->binary_op));
            ast_dump_expr(expr->right, out, 0);
            fprintf(out, ")");
        }
        break;
    case EXPR_ASSIGN:
        ast_dump_expr(expr->left, out, 0);
        fprintf(out, " = ");
        ast_dump_expr(expr->right, out, 0);
        break;
    case EXPR_CALL:
        ast_dump_expr(expr->callee, out, 0);
        fprintf(out, "(");
        first = true;
        list_for_each_entry (arg, &expr->args, node) {
            if (!first)
                fprintf(out, ", ");
            ast_dump_expr(arg, out, 0);
            first = false;
        }
        fprintf(out, ")");
        break;
    case EXPR_MEMBER:
        ast_dump_expr(expr->object, out, 0);
        if (expr->is_arrow)
            fprintf(out, "->");
        else
            fprintf(out, ".");
        if (expr->member)
            fprintf(out, "%s", expr->member->name);
        break;
    case EXPR_CAST:
        fprintf(out, "(");
        ast_dump_type(expr->cast_type, out);
        fprintf(out, ")");
        ast_dump_expr(expr->cast_operand, out, 0);
        break;
    case EXPR_TERNARY:
        fprintf(out, "(");
        ast_dump_expr(expr->cond, out, 0);
        fprintf(out, " ? ");
        ast_dump_expr(expr->then_expr, out, 0);
        fprintf(out, " : ");
        ast_dump_expr(expr->else_expr, out, 0);
        fprintf(out, ")");
        break;
    case EXPR_STMT_EXPR:
        fprintf(out, "({\n");
        if (expr->stmt_expr_body)
            ast_dump_stmt(expr->stmt_expr_body, out, indent + 1);
        ast_dump_indent(out, indent);
        fprintf(out, "})");
        break;
    case EXPR_GENERIC:
        fprintf(out, "_Generic(");
        ast_dump_expr(expr->generic_control, out, 0);
        list_for_each_entry (assoc, &expr->generic_assoc, node) {
            fprintf(out, ", ");
            if (assoc->type)
                ast_dump_type(assoc->type, out);
            else
                fprintf(out, "default");
            fprintf(out, ": ");
            ast_dump_expr(assoc->expr, out, 0);
        }
        fprintf(out, ")");
        break;
    default:
        fprintf(out, "<unknown-expr>");
        break;
    }
}

void ast_dump_stmt(struct ast_stmt *stmt, FILE *out, int indent)
{
    struct ast_stmt *child = NULL;

    if (!stmt || !out)
        return;

    switch (stmt->stmt_kind) {
    case STMT_COMPOUND:
        ast_dump_indent(out, indent);
        fprintf(out, "{\n");
        list_for_each_entry (child, &stmt->stmts, node)
            ast_dump_stmt(child, out, indent + 1);
        ast_dump_indent(out, indent);
        fprintf(out, "}\n");
        break;
    case STMT_EXPR:
        ast_dump_indent(out, indent);
        if (stmt->expr)
            ast_dump_expr(stmt->expr, out, indent);
        fprintf(out, ";\n");
        break;
    case STMT_DECL:
        ast_dump_indent(out, indent);
        if (stmt->stmt_decl) {
            ast_dump_decl(stmt->stmt_decl, out, indent);
            fprintf(out, ";\n");
        }
        break;
    case STMT_IF:
        ast_dump_indent(out, indent);
        fprintf(out, "if (");
        ast_dump_expr(stmt->if_cond, out, 0);
        fprintf(out, ")\n");
        ast_dump_stmt(stmt->then_stmt, out, indent + 1);
        if (stmt->else_stmt) {
            ast_dump_indent(out, indent);
            fprintf(out, "else\n");
            ast_dump_stmt(stmt->else_stmt, out, indent + 1);
        }
        break;
    case STMT_WHILE:
        ast_dump_indent(out, indent);
        fprintf(out, "while (");
        ast_dump_expr(stmt->loop_cond, out, 0);
        fprintf(out, ")\n");
        ast_dump_stmt(stmt->loop_body, out, indent + 1);
        break;
    case STMT_DO_WHILE:
        ast_dump_indent(out, indent);
        fprintf(out, "do\n");
        ast_dump_stmt(stmt->loop_body, out, indent + 1);
        ast_dump_indent(out, indent);
        fprintf(out, "while (");
        ast_dump_expr(stmt->loop_cond, out, 0);
        fprintf(out, ");\n");
        break;
    case STMT_FOR:
        ast_dump_indent(out, indent);
        fprintf(out, "for (");
        if (stmt->for_init_stmt) {
            if (stmt->for_init_stmt->stmt_kind == STMT_DECL &&
                stmt->for_init_stmt->stmt_decl)
                ast_dump_decl(stmt->for_init_stmt->stmt_decl, out, 0);
            else if (stmt->for_init_stmt->expr)
                ast_dump_expr(stmt->for_init_stmt->expr, out, 0);
        } else if (stmt->for_init_expr) {
            ast_dump_expr(stmt->for_init_expr, out, 0);
        }
        fprintf(out, "; ");
        if (stmt->for_cond)
            ast_dump_expr(stmt->for_cond, out, 0);
        fprintf(out, "; ");
        if (stmt->for_step)
            ast_dump_expr(stmt->for_step, out, 0);
        fprintf(out, ")\n");
        ast_dump_stmt(stmt->for_body, out, indent + 1);
        break;
    case STMT_RETURN:
        ast_dump_indent(out, indent);
        fprintf(out, "return");
        if (stmt->ret_expr) {
            fprintf(out, " ");
            ast_dump_expr(stmt->ret_expr, out, 0);
        }
        fprintf(out, ";\n");
        break;
    case STMT_BREAK:
        ast_dump_indent(out, indent);
        fprintf(out, "break;\n");
        break;
    case STMT_CONTINUE:
        ast_dump_indent(out, indent);
        fprintf(out, "continue;\n");
        break;
    case STMT_GOTO:
        ast_dump_indent(out, indent);
        fprintf(out, "goto %s;\n",
                stmt->label_name ? stmt->label_name->name : "<unknown>");
        break;
    case STMT_LABEL:
        ast_dump_indent(out, indent > 0 ? indent - 1 : 0);
        fprintf(out, "%s:\n",
                stmt->label_name ? stmt->label_name->name : "<unknown>");
        break;
    case STMT_CASE:
        ast_dump_indent(out, indent);
        fprintf(out, "case ");
        ast_dump_expr(stmt->case_val, out, 0);
        fprintf(out, ":\n");
        break;
    case STMT_DEFAULT:
        ast_dump_indent(out, indent);
        fprintf(out, "default:\n");
        break;
    case STMT_SWITCH:
        ast_dump_indent(out, indent);
        fprintf(out, "switch (");
        ast_dump_expr(stmt->switch_cond, out, 0);
        fprintf(out, ")\n");
        ast_dump_stmt(stmt->switch_body, out, indent + 1);
        break;
    case STMT_STATIC_ASSERT:
        ast_dump_indent(out, indent);
        fprintf(out, "_Static_assert(");
        ast_dump_expr(stmt->assert_cond, out, 0);
        if (stmt->assert_msg)
            fprintf(out, ", \"%s\"", stmt->assert_msg);
        fprintf(out, ");\n");
        break;
    case STMT_ASM:
        ast_dump_indent(out, indent);
        fprintf(out, "asm volatile(\"%s\");\n",
                stmt->asm_code ? stmt->asm_code : "");
        break;
    default:
        ast_dump_indent(out, indent);
        fprintf(out, "<unknown-stmt>;\n");
        break;
    }
}

void ast_dump_decl(struct ast_decl *decl, FILE *out, int indent)
{
    if (!decl || !out)
        return;

    if (decl->storage_class && decl->storage_class != sym_dump)
        fprintf(out, "%s ", token_name(decl->storage_class));

    if (decl->is_inline)
        fprintf(out, "inline ");
    if (decl->is_noreturn)
        fprintf(out, "_Noreturn ");

    if (decl->cleanup_fn)
        fprintf(out, "__attribute__((cleanup(%s))) ", decl->cleanup_fn->name);

    if (decl->type) {
        ast_dump_type(decl->type, out);
        if (decl->id)
            fprintf(out, "%s", decl->id->name);
    } else if (decl->id) {
        fprintf(out, "%s", decl->id->name);
    }

    if (decl->init) {
        fprintf(out, " = ");
        ast_dump_expr(decl->init, out, indent);
    }

    if (decl->body) {
        fprintf(out, "\n");
        ast_dump_stmt(decl->body, out, indent);
    }
}

void ast_dump_translation_unit(struct ast_translation_unit *tu, FILE *out)
{
    struct ast_decl *decl = NULL;

    if (!tu || !out)
        return;

    list_for_each_entry (decl, &tu->decls, node) {
        ast_dump_decl(decl, out, 0);
        if (!decl->body)
            fprintf(out, ";\n");
        else
            fprintf(out, "\n");
    }
}
