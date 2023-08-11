#include <osc/compiler.h>
#include <osc/list.h>
#include <osc/debug.h>
#include <osc/parser.h>

static struct symbol sym_table[] = {
    /* attribute */
    __SYM_ENTRY(__brw, sym_attr_brw),
    __SYM_ENTRY(__mut, sym_attr_mut),
    __SYM_ENTRY(__clone, sym_attr_clone),
 
    /* storage type */
    SYM_ENTRY(auto),
    SYM_ENTRY(register),
    SYM_ENTRY(static),
    SYM_ENTRY(extern),

    /* type */
    SYM_ENTRY(int),
    SYM_ENTRY(short),
    SYM_ENTRY(long),
    __SYM_ENTRY(long long, sym_long_long),
    __SYM_ENTRY(unsigned int, sym_unsigned_int),
    __SYM_ENTRY(unsigned short, sym_unsigned_short),
    __SYM_ENTRY(unsigned long, sym_unsigned_long),
    __SYM_ENTRY(unsigned long long, sym_unsigned_long_long),
    SYM_ENTRY(char),
    __SYM_ENTRY(signed char, sym_signed_char),
    __SYM_ENTRY(unsigned char, sym_unsigned_char),
    SYM_ENTRY(double),
    __SYM_ENTRY(long double, sym_long_double),
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

    /* sym id start */
    __SYM_ENTRY(->, sym_ptr_assign),
    __SYM_ENTRY(||, sym_logic_or),
    __SYM_ENTRY(&&, sym_logic_and),
    __SYM_ENTRY(==, sym_equal),
};

static int sym_one_char(struct scan_file_control *sfc)
{
    int sym = sym_dump;
    switch (sfc->buffer[sfc->offset]) {
    case '(':
        sym = sym_left_paren;
        break;
    case ')':
        sym = sym_right_paren;
        break;
    case '{':
        sym = sym_left_brace;
        break;
    case '}':
        sym = sym_right_brace;
        break;
    case '[':
        sym = sym_left_sq_brace;
        break;
    case ']':
        sym = sym_right_sq_brace;
        break;
    case '*':
        sym = sym_aster;
        break;
    case '<':
        sym = sym_lt;
        break;
    case '>':
        sym = sym_gt;
        break;
    case '=':
        sym = sym_eq;
        break;
    case '+':
        sym = sym_add;
        break;
    case '-':
        sym = sym_minus;
        break;
    case ',':
        sym = sym_comma;
        break;
    case '.':
        sym = sym_dot;
        break;
    case ';':
        sym = sym_seq_point;
        break;
    default:
        //pr_debug("%c\n", sfc->buffer[sfc->offset]);
        goto failed;
    }
    sfc->offset++;
failed:
    return sym;
}

static __always_inline int check_symbol(struct scan_file_control *sfc,
                                        struct symbol *sym)
{
    if (sym->len > buffer_rest(sfc))
        return 0;
    //pr_debug("check (%s, %u): %s", sym->name, sym->len, &sfc->buffer[sfc->offset]);
    if (strncmp(&sfc->buffer[sfc->offset], sym->name, sym->len) == 0) {
        sfc->offset += sym->len;
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

//
// We have two type of comments:
// first:  //
// second: /* ... */
//          ^      ^
//          |      \_step2
//          \_step1
//
static int skip_comments(struct scan_file_control *sfc)
{
    int step_1 = 0;

again:
    buffer_for_each (sfc) {
        if (ch == '/') {
            /* check the first type */
            if (sfc->offset + 1 < sfc->size && sfc->offset + 1 != '\n') {
                sfc->offset++;
                if (sfc->buffer[sfc->offset] == '/') {
                    if (!next_line(sfc))
                        return -ENODATA;
                    /* try_decode_action will inc offset, so rollback here. */
                    sfc->offset--;
                    return -EAGAIN;
                } else if (sfc->buffer[sfc->offset] == '*') {
                    /* Enter the step 1 */
                    step_1 = 1;
                } else {
                    /* We are not the comment symbols, rollback */
                    sfc->offset--;
                    return 0;
                }
            }
        } else if (ch == '*' && step_1) {
            if (sfc->offset + 1 < sfc->size && sfc->offset + 1 != '\n') {
                sfc->offset++;
                if (sfc->buffer[sfc->offset] == '/') {
                    return -EAGAIN;
                }
            }
        } else if (!step_1)
            return 0;
        /*
         * If we are step 1, we don't have to drop the progress
         * since the comment can be the different line.
         */
    }

    if (step_1) {
        if (next_line(sfc))
            goto again;
        bad(sfc, "non-closed comment");
        return -ENODATA;
    }

    return 0;
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
            sym_one_char(sfc) != sym_dump) {
            /*
             * Following show the offsets value,
             * so the len of id is sfc->offset - offset + 1:
             *
             *    +-- offset            +-- sfc->offset
             *    V                     V
             *  [ i d e n t i f i e r K D ]
             *
             *  - K is the keyword, "(", "{", etc.
             *  - D, dump char, after K.
             */
            sfc->offset--;
            goto get_id;
        }
        if (blank(sfc->buffer[sfc->offset]))
            goto get_id;
        sfc->offset++;
    }

    return sym_dump;

get_id:
    /*
     *
     *    +-- offset          +-- sfc->offset
     *    V                   V
     *  [ i d e n t i f i e r K D ]
     *
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
    (*id)->name[(*id)->len] = '\0';

out:
    return sym_id;
}

static int __get_token(struct scan_file_control *sfc, struct symbol **id)
{
    int sym = sym_dump;

    if (skip_comments(sfc))
        return -EAGAIN;

    /* check the keyword */
    sym = check_symbol_table(sfc, id);
    if (sym != sym_dump)
        goto out;

    /* check one word, should check after the table */
    sym = sym_one_char(sfc);
    if (sym != sym_dump)
        goto out;

    /* TODO: check num. */

    sym = insert_sym_id(sfc, id);
    if (sym == sym_dump)
        sym = -EAGAIN;
out:
    return sym;
}

int get_token(struct scan_file_control *sfc, struct symbol **id)
{
    *id = NULL;
    return try_decode_action(sfc, __get_token, id);
}

int cmp_token(struct symbol *l, struct symbol *r)
{
    BUG_ON(l == NULL, "null ptr");
    BUG_ON(r == NULL, "null ptr");
    if (l->flags != r->flags)
        return 0;
    if (!(strncmp(l->name, r->name, max(l->len, r->len)) == 0))
        return 0;
    return 1;
}

const char *token_name(int n)
{
    BUG_ON(n < 0 || n >= ARRAY_SIZE(sym_table), "out of scope:%d", n);

    return sym_table[n].name;
}

static unsigned long random_generation = 0;

struct symbol *new_random_symbol(void)
{
    char *buffer = NULL;
    unsigned long buffer_size = 128;
    unsigned long seed;
    struct symbol_id_struct *symbol_id;

    symbol_id = malloc(sizeof(struct symbol_id_struct));
    BUG_ON(!symbol_id, "malloc");

    buffer = malloc(buffer_size * sizeof(char));
    BUG_ON(!buffer, "malloc");

    seed = random_generation++;
    snprintf(buffer, buffer_size, "#auto_generated_anon_%lu#", seed);
    buffer[buffer_size - 1] = '\0';

    symbol_id->sym.name = buffer;
    symbol_id->sym.len = strlen(buffer);
    symbol_id->sym.flags = sym_id;

    list_add_tail(&symbol_id->node, &symbol_id_container.head);

    return &symbol_id->sym;
}
