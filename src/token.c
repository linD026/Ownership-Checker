#include <osc/compiler.h>
#include <osc/list.h>
#include <osc/debug.h>
#include <osc/parser.h>

static struct symbol sym_table[] = {
    /* attribute */
    __SYM_ENTRY(__brw, sym_attr_brw),
    __SYM_ENTRY(__mut, sym_attr_mut),
    __SYM_ENTRY(__clone, sym_attr_clone),

    /* prefix type */
    SYM_ENTRY(unsigned),
    SYM_ENTRY(signed),
 
    /* storage type */
    SYM_ENTRY(auto),
    SYM_ENTRY(register),
    SYM_ENTRY(static),
    SYM_ENTRY(extern),

    /* type */
    SYM_ENTRY(int),
    SYM_ENTRY(long),
    SYM_ENTRY(short),
    SYM_ENTRY(char),
    SYM_ENTRY(double),
    SYM_ENTRY(float),
    SYM_ENTRY(struct),
    SYM_ENTRY(void),

    /* other keywords */
    SYM_ENTRY(do),
    SYM_ENTRY(while),
    SYM_ENTRY(for),
    SYM_ENTRY(if),
    SYM_ENTRY(else),
    SYM_ENTRY(switch),
    SYM_ENTRY(case),
    SYM_ENTRY(return),
    SYM_ENTRY(true),
    SYM_ENTRY(false),

    /* Allocate function */
    SYM_ENTRY(malloc),
    SYM_ENTRY(free),

    /* sym id start */
    __SYM_ENTRY(->, sym_ptr_assign),
    __SYM_ENTRY(||, sym_logic_or),
    __SYM_ENTRY(&&, sym_logic_and),
};

static int sym_one_char(struct scan_file_control *sfc)
{
    switch (sfc->buffer[sfc->offset]) {
    case '(':
        return sym_left_paren;
    case ')':
        return sym_right_paren;
    case '{':
        return sym_left_brace;
    case '}':
        return sym_right_brace;
    case '[':
        return sym_left_sq_brace;
    case ']':
        return sym_right_sq_brace;
    case '*':
        return sym_aster;
    case '<':
        return sym_lt;
    case '>':
        return sym_gt;
    case '=':
        return sym_eq;
    case ',':
        return sym_comma;
    case '.':
        return sym_dot;
    case ';':
        return sym_seq_point;
    default:
        //pr_debug("%c\n", sfc->buffer[sfc->offset]);
        return sym_dump;
    }
    return sym_dump;
}

static __always_inline int check_symbol(struct scan_file_control *sfc,
                                        struct symbol *sym)
{
    if (sym->len > buffer_rest(sfc))
        return 0;
    //pr_debug("check (%s, %u): %s", sym->name, sym->len, &sfc->buffer[sfc->offset]);
    if (strncmp(&sfc->buffer[sfc->offset], sym->name, sym->len) == 0) {
        sfc->offset += sym->len - 1;
        return 1;
    }
    return 0;
}

static int __check_symbol_table(struct scan_file_control *sfc,
                                struct symbol **id, int table_start)
{
    int sym = sym_dump;

    for (int i = table_start; i < ARRAY_SIZE(sym_table); i++) {
        if (check_symbol(sfc, &sym_table[i])) {
            *id = &sym_table[i];
            sym = sym_table[i].flags;
            goto out;
        }
    }
out:
    return sym;
}

static __always_inline int check_symbol_table(struct scan_file_control *sfc,
                                              struct symbol **id)
{
    return __check_symbol_table(sfc, id, 0);
}

static int __skip_comments(struct scan_file_control *sfc)
{
    if (strncmp("//", &sfc->buffer[sfc->offset], 2) == 0) {
        if (!next_line(sfc))
            return -ENODATA;
        /* try_decode_action will inc offset, so we roll back here. */
        sfc->offset--;
        return -EAGAIN;
    }
    return 0;
}

static int skip_comments(struct scan_file_control *sfc)
{
    return try_decode_action(sfc, __skip_comments);
}

struct symbol_id_struct {
    struct symbol sym;
    struct list_head node;
};

struct symbol_id_container {
    struct list_head head;
};

static struct symbol_id_container symbol_id_container = {
    .head = LIST_HEAD_INIT(symbol_id_container.head),
};

void symbol_id_container_release(void)
{
    list_for_each_safe (&symbol_id_container.head) {
        struct symbol_id_struct *symbol_id =
            container_of(curr, struct symbol_id_struct, node);
        free(symbol_id);
    }
}

static struct symbol *search_sym_id(char *id, unsigned int len)
{
    list_for_each (&symbol_id_container.head) {
        struct symbol_id_struct *symbol_id =
            container_of(curr, struct symbol_id_struct, node);
        if (strncmp(id, symbol_id->sym.name, max(symbol_id->sym.len, len)) == 0)
            return &symbol_id->sym;
    }

    return NULL;
}

static int insert_sym_id(struct scan_file_control *sfc, struct symbol **id)
{
    struct symbol_id_struct *symbol_id = NULL;
    unsigned int orig_offset = sfc->offset;

    while (!line_end(sfc)) {
        if (__check_symbol_table(sfc, id, sym_id_start) != sym_dump ||
            sym_one_char(sfc) != sym_dump ||
            blank(sfc->buffer[sfc->offset])) {
            break;
        }
        sfc->offset++;
    }

    /*
     * Following show the offsets value, so the len of id is sfc->offset - offset + 1:
     *    +-- offset        +-- sfc->offset
     *    V                 V
     *  [ i d e n t i f i e r K ]
     *
     *  - K is the keyword.
     */
    *id = search_sym_id(&sfc->buffer[orig_offset], sfc->offset - orig_offset);
    if (*id)
        goto out;

    symbol_id = malloc(sizeof(struct symbol_id_struct));
    BUG_ON(!symbol_id, "malloc");
    list_add_tail(&symbol_id->node, &symbol_id_container.head);
    *id = &symbol_id->sym;

    (*id)->flags = sym_id;
    (*id)->len = sfc->offset - orig_offset;
    /* Don't foget the terminal. */
    (*id)->name = malloc(symbol_id->sym.len + 1);
    BUG_ON(!(*id)->name, "malloc");
    strncpy((*id)->name, &sfc->buffer[orig_offset], (*id)->len);

out:
    /* if we had the offset, roll back */
    if (sfc->offset != orig_offset)
        sfc->offset--;
    return sym_id;
}

static int __get_token(struct scan_file_control *sfc, struct symbol **id)
{
    int sym = sym_dump;

    if (skip_comments(sfc))
        return sym;

    /* check the keyword */
    sym = check_symbol_table(sfc, id);
    if (sym != sym_dump)
        goto out;

    /* check one word, should check after the table */
    sym = sym_one_char(sfc);
    if (sym != sym_dump)
        goto out;

    /* check num. */


    sym = insert_sym_id(sfc, id);
    if (sym == sym_dump)
        sym = -EAGAIN;
out:
    return sym;
}

int get_token(struct scan_file_control *sfc, struct symbol **id)
{
    return try_decode_action(sfc, __get_token, id);
}
