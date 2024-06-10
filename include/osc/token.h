#ifndef __OSC_TOKEN_H__
#define __OSC_TOKEN_H__

#include <osc/compiler.h>

enum {
    sym_dump = -1,

/* sym_table start */

/* attr start - brw */
#define sym_attr_start sym_attr_brw
    sym_attr_brw,
    sym_attr_mut,
    sym_attr_clone,
#define sym_attr_end sym_attr_clone
/* attr end - clone */

/* sym storage class start - auto */
#define sym_storage_class_start sym_auto
    sym_auto,
    sym_register,
    sym_static,
    sym_extern,
#define sym_storage_class_end sym_extern
/* sym storage class end - extern */

/* sym qualifier class start - const */
#define sym_qualifier_start sym_const
    sym_const,
    sym_volatile,
    sym_restrict,
    sym__Atomic,
#define sym_qualifier_end sym__Atomic
/* sym qualifier class end - _Atomic */

/* sym type start - int */
#define sym_type_start sym_int
    sym_int,
    sym_short,
    sym_long,
    sym_long_long,
    sym_unsigned_int,
    sym_unsigned_short,
    sym_unsigned_long,
    sym_unsigned_long_long,
    sym_char,
    sym_signed_char,
    sym_unsigned_char,
    sym_double,
    sym_long_double,
    sym_float,
    sym_struct,
    sym_void,
#define sym_type_end sym_void
    /* sym type end - void */

    /* other multiple char keywords */
    sym_typedef,
    sym_do,
    sym_while,
    sym_for,
    sym_if,
    sym_else,
    sym_switch,
    sym_case,
    sym_return,
    sym_true, /* C23 keyword true, false */
    sym_false,

/* Decode the id until the symbol is between sym_id_start to sym_id_end. */
/* sym id start - ptr_assign */
#define sym_id_start sym_ptr_assign
    sym_ptr_assign, // ->
    sym_logic_or, // ||
    sym_logic_and, // &&
    sym_equal, // ==

/* sym_table end */

/* sym one char start - left_paren */
#define sym_one_char_start sym_left_paren
    sym_left_paren, // ()
    sym_right_paren,
    sym_left_brace, // {}
    sym_right_brace,
    sym_left_sq_brace, // []
    sym_right_sq_brace,
    sym_aster, // *
    sym_lt, // <
    sym_gt, // >
    sym_eq, // =
    sym_add, // +
    sym_minus, // -
    sym_quotation, // "
    sym_bit_and, // &
    sym_bit_or, // |
    sym_comma, // ,
    sym_dot, // .
    sym_seq_point, // ;
#define sym_one_char_end sym_seq_point
/* sym one char end - seq point */
#define sym_id_end sym_seq_point
    /* sym id end - seq point */

    /* constant */
    sym_numeric_constant,
    sym_string_literals,

    /* id */
    sym_id,
};

#define range_in_sym(range_name, number) \
    (sym_##range_name##_start <= number && number <= sym_##range_name##_end)

struct scan_file_control;
struct symbol;

int token_init(struct scan_file_control *sfc);
void symbol_id_container_release(void);
int get_token(struct scan_file_control *sfc, struct symbol **id);
int cmp_token(struct symbol *l, struct symbol *r);
const char *token_name(int n);

struct symbol *new_anon_symbol(void);

int peak_token(struct scan_file_control *sfc, struct symbol **id);

static inline void flush_peak_token(struct scan_file_control *sfc)
{
    struct symbol *symbol;
    get_token(sfc, &symbol);
}

#endif /* __OSC_TOKEN_H__ */
