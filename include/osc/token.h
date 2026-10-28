#ifndef __OSC_TOKEN_H__
#define __OSC_TOKEN_H__

#include <stdio.h>
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
    sym__Thread_local,
#define sym_storage_class_end sym__Thread_local
/* sym storage class end - _Thread_local */

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
    sym_union,
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
    sym_default,
    sym_goto,
    sym_break,
    sym_continue,
    sym_return,
    sym_true, /* C23 keyword true, false */
    sym_false,
    sym_inline,
    sym___inline__,
    sym__Noreturn,
    sym__Alignas,
    sym__Alignof,
    sym__Static_assert,
    sym__Generic,
    sym_asm,
    sym___asm__,
    sym___attribute__,

/* Decode the id until the symbol is between sym_id_start to sym_id_end. */
/* sym id start - ptr_assign */
#define sym_id_start sym_ptr_assign
    sym_ptr_assign, // ->
    sym_logic_or, // ||
    sym_logic_and, // &&
    sym_equal, // ==
    sym_not_equal, // !=
    sym_le, // <=
    sym_ge, // >=
    sym_bit_left, // <<
    sym_bit_right, // >>
    sym_inc, // ++
    sym_dec, // --

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
    sym_slash, // /
    sym_percent, // %
    sym_lt, // <
    sym_gt, // >
    sym_eq, // =
    sym_add, // +
    sym_minus, // -
    sym_quotation, // "
    sym_bit_and, // &
    sym_bit_or, // |
    sym_bit_xor, // ^
    sym_bit_not, // ~
    sym_exclaim, // !
    sym_query, // ?
    sym_colon, // :
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

struct osc_span {
    unsigned long line;
    unsigned int col;
    unsigned int len;
    unsigned long offset;
};

struct osc_token {
    int sym;
    struct symbol *symbol;
    struct osc_span span;
};

struct osc_context;

struct token_stream {
    struct osc_token *tokens;
    size_t count;
    size_t capacity;
    size_t pos;
    struct osc_context *ctx;
};

int token_stream_init(struct token_stream *ts, struct osc_context *ctx);
int token_stream_tokenize_file(struct token_stream *ts, FILE *fp,
                               const char *filename);
struct osc_token *tok_peek(struct token_stream *ts, size_t lookahead);
struct osc_token *tok_consume(struct token_stream *ts);
bool tok_match(struct token_stream *ts, int sym);

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
