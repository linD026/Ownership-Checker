#include <osc/compiler.h>
#include <osc/list.h>
#include <osc/debug.h>
#include <osc/parser.h>

#define __SYM_ENTRY(_name, _flags) \
    [_flags] = { .name = #_name, .len = sizeof(#_name) - 1, .flags = _flags }

#define SYM_ENTRY(_name) __SYM_ENTRY(_name, sym_##_name)

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

    /* qualifier type */
    SYM_ENTRY(const),
    SYM_ENTRY(volatile),
    SYM_ENTRY(restrict),
    SYM_ENTRY(_Atomic),

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

static int __check_sym_one_char(struct scan_file_control *sfc)
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
    case '"':
        sym = sym_quotation;
        break;
    case '&':
        sym = sym_bit_and;
        break;
    case '|':
        sym = sym_bit_or;
        break;
    case ',':
        sym = sym_comma;
        break;
    case '.':
        sym = sym_dot;
        break;
    case '#':
        sym = sym_ns;
        break;
    case ';':
        sym = sym_seq_point;
        break;
        //pr_debug("%c\n", sfc->buffer[sfc->offset]);
    }
    return sym;
}

static int check_sym_one_char(struct scan_file_control *sfc)
{
    int sym = __check_sym_one_char(sfc);
    if (sym != sym_dump)
        sfc->offset++;
    return sym;
}

/*
 * We don't check the terminal symbol '\0', since we should make sure that
 * each time we iterate the buffer we won't skip any symbol.
 * But for some times (i.e., debugging), we can add this.
 * (sfc)->buffer[(sfc)->offset] == '\0' ||\
 */
#define line_end(sfc) \
    ((sfc)->offset >= (sfc)->size || (sfc)->buffer[(sfc)->offset] == '\n')

#define buffer_for_each(sfc)                                     \
    for (char ch = (sfc)->buffer[(sfc)->offset]; !line_end(sfc); \
         ch = (sfc)->buffer[++(sfc)->offset])

#define buffer_rest(sfc) ((sfc)->size - (sfc)->offset + 1)

/*
 * It will get the next non-blank symbols. it's same as following function:
 *
 *   static __always_inline int decode_obj_action(
 *       struct scan_file_control *sfc, struct object_struct *obj,
 *       int (*action)(struct scan_file_control *, struct object_struct *))
 *   {
 *       if (!action)
 *           return -EINVAL;
 *   again:
 *       buffer_for_each (sfc) {
 *           if (blank(ch))
 *               continue;
 *           if (!action(sfc, obj))
 *               return 0;
 *       }
 *       next_line(sfc);
 *       goto again;
 *   }
 *
 */
#define ___decode_action(sfc, action, id, ret, ...) \
    do {                                            \
        da_##id##_again : buffer_for_each (sfc)     \
        {                                           \
            if (blank(ch))                          \
                continue;                           \
            ret = action(sfc, ##__VA_ARGS__);       \
            if (ret != -EAGAIN)                     \
                goto da_##id##_out;                 \
        }                                           \
        if (next_line(sfc))                         \
            goto da_##id##_again;                   \
        else                                        \
            ret = -ENODATA;                         \
        da_##id##_out:;                             \
    } while (0)

#define __decode_action(sfc, action, id, ...)                                \
    ({                                                                       \
        int __da_ret = 0;                                                    \
        ___decode_action(sfc, action, id, __da_ret, ##__VA_ARGS__);          \
        WARN_ON(__da_ret < 0 && __da_ret != -EAGAIN,                         \
                "action:%s, error:%s, buf:%s", #action, strerror(-__da_ret), \
                &sfc->buffer[sfc->offset]);                                  \
        __da_ret;                                                            \
    })

#define decode_action(sfc, action, ...) \
    __decode_action(sfc, action, __LINE__, ##__VA_ARGS__)

#define __try_decode_action(sfc, action, id, ...)                   \
    ({                                                              \
        int __da_ret = 0;                                           \
        ___decode_action(sfc, action, id, __da_ret, ##__VA_ARGS__); \
        __da_ret;                                                   \
    })

#define try_decode_action(sfc, action, ...) \
    __try_decode_action(sfc, action, __LINE__, ##__VA_ARGS__)

static __always_inline int check_symbol(struct scan_file_control *sfc,
                                        struct symbol *sym)
{
    if (sym->len > buffer_rest(sfc))
        return 0;
    if (strncmp(&sfc->buffer[sfc->offset], sym->name, sym->len) == 0) {
        //pr_debug("check (%s, %u): %s", sym->name, sym->len, &sfc->buffer[sfc->offset]);
        sfc->offset += sym->len;
        if (buffer_rest(sfc)) {
            if (!blank(sfc->buffer[sfc->offset]) &&
                __check_sym_one_char(sfc) == sym_dump) {
                sfc->offset -= sym->len;
                return 0;
            }
        }
        return sym->len;
    }
    return 0;
}

static int __check_symbol_table(struct scan_file_control *sfc,
                                struct symbol **id, int table_start, int *len)
{
    int sym = sym_dump;

    for (int i = table_start; i < ARRAY_SIZE(sym_table); i++) {
        int tmp = check_symbol(sfc, &sym_table[i]);
        if (tmp) {
            *id = &sym_table[i];
            sym = sym_table[i].flags;
            *len = tmp;
            goto out;
        }
    }
out:
    return sym;
}

static __always_inline int check_symbol_table(struct scan_file_control *sfc,
                                              struct symbol **id, int *len)
{
    return __check_symbol_table(sfc, id, 0, len);
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
    int len = 0;

    while (!line_end(sfc)) {
        if (__check_symbol_table(sfc, id, sym_id_start, &len) != sym_dump) {
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
            sfc->offset -= len;
            goto get_id;
        } else if (__check_sym_one_char(sfc) != sym_dump)
            goto get_id;
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

static int get_string_literals(struct scan_file_control *sfc,
                               struct symbol **id)
{
    pr_debug("string literals start\n");
again:
    buffer_for_each (sfc) {
        if (ch == '"') {
#ifdef CONFIG_DEBUG
            print("\n");
#endif
            pr_debug("string literals end\n");
            sfc->offset++;
            return sym_string_literals;
        }
#ifdef CONFIG_DEBUG
        print("%c", ch);
#endif
    }
    if (next_line(sfc))
        goto again;
    return -EAGAIN;
}

static int __get_token(struct scan_file_control *sfc, struct symbol **id)
{
    int sym = sym_dump;
    int len = 0;

    if (skip_comments(sfc))
        return -EAGAIN;

    /* check the keyword */
    sym = check_symbol_table(sfc, id, &len);
    if (sym != sym_dump)
        goto out;

    /* check one word, should check after the table */
    sym = check_sym_one_char(sfc);
    if (sym == sym_ns) {
        next_line(sfc);
        /*
         * try_decode_action() will increase
         * sfc->offset so we rollback here.
         */
        sfc->offset--;
        return -EAGAIN;
    }
    if (sym != sym_dump && sym != sym_quotation)
        goto out;

    /* TODO: check num. */
    if (sym == sym_quotation) {
        sym = get_string_literals(sfc, id);
        if (sym != sym_dump)
            goto out;
    }

    sym = insert_sym_id(sfc, id);
    if (sym == sym_dump)
        sym = -EAGAIN;
out:
    return sym;
}

struct peak_token_info {
    struct symbol *symbol;
    int sym;
    struct list_head peak_node;
};

int get_token(struct scan_file_control *sfc, struct symbol **id)
{
    if (sfc->peak) {
        struct peak_token_info *pti;
        int sym = sym_dump;

        pr_debug("Pop the peak token (%d) from the list\n", sfc->peak);

        BUG_ON(list_empty(&sfc->peak_head), "peak != 0 but list is empty");
        pti = list_first_entry(&sfc->peak_head, struct peak_token_info,
                               peak_node);
        *id = pti->symbol;
        sym = pti->sym;
        list_del(&pti->peak_node);
        free(pti);
        sfc->peak--;
        return sym;
    }

    *id = NULL;
    return try_decode_action(sfc, __get_token, id);
}

/*
 * After called the peak_token(), in order to get the next token which
 * is behind the peak token, we can use flush_peak_token() to flush
 * the token we peak.
 */
int peak_token(struct scan_file_control *sfc, struct symbol **id)
{
    int ret = sym_dump;
    struct peak_token_info *pti = malloc(sizeof(struct peak_token_info));
    BUG_ON(!pti, "malloc");

    *id = NULL;
    ret = try_decode_action(sfc, __get_token, id);
    if (ret == -ENODATA) {
        free(pti);
        return ret;
    }

    pti->symbol = *id;
    pti->sym = ret;
    list_add(&pti->peak_node, &sfc->peak_head);
    sfc->peak++;

    pr_debug("Push the peak token (%d) to the list\n", sfc->peak);

    return ret;
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

struct symbol *new_anon_symbol(void)
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
