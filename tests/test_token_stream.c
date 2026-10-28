#include <osc/parser.h>
#include <osc/token.h>
#include <osc/context.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

int main(void)
{
    struct file_info fi;
    struct osc_context ctx;
    struct token_stream ts;
    struct osc_token *tok = NULL;
    FILE *tmp = NULL;
    const char *code = "int main(void) {\n"
                       "    int a = 10;\n"
                       "    return a;\n"
                       "}\n";

    tmp = tmpfile();
    assert(tmp != NULL);
    fputs(code, tmp);
    rewind(tmp);

    memset(&fi, 0, sizeof(fi));
    fi.file = tmp;
    strcpy(fi.name, "stream_test.c");
    strcpy(fi.generated_name, "stream_test.c");

    osc_context_init(&ctx, &fi, OSC_MODE_AUTO);
    token_stream_init(&ts, &ctx);

    token_stream_tokenize_file(&ts, tmp, "stream_test.c");
    assert(ts.count > 0);

    /* 1. Verify lookahead O(1) */
    tok = tok_peek(&ts, 0);
    assert(tok != NULL && tok->sym == sym_int);

    tok = tok_peek(&ts, 1);
    assert(tok != NULL && tok->sym == sym_id);
    assert(strcmp(tok->symbol->name, "main") == 0);

    tok = tok_peek(&ts, 2);
    assert(tok != NULL && tok->sym == sym_left_paren);

    /* 2. Consume and match */
    tok = tok_consume(&ts);
    assert(tok->sym == sym_int);

    assert(tok_match(&ts, sym_id) == true);
    assert(tok_match(&ts, sym_left_paren) == true);
    assert(tok_match(&ts, sym_void) == true);
    assert(tok_match(&ts, sym_right_paren) == true);
    assert(tok_match(&ts, sym_left_brace) == true);

    /* 3. Next statement: int a = 10; */
    assert(tok_match(&ts, sym_int) == true);
    tok = tok_consume(&ts);
    assert(tok->sym == sym_id && strcmp(tok->symbol->name, "a") == 0);
    assert(tok_match(&ts, sym_eq) == true);

    tok = tok_consume(&ts); /* 10 is numeric or id */
    assert(tok_match(&ts, sym_seq_point) == true);

    /* 4. return a; } */
    assert(tok_match(&ts, sym_return) == true);
    tok = tok_consume(&ts);
    assert(tok->sym == sym_id && strcmp(tok->symbol->name, "a") == 0);
    assert(tok_match(&ts, sym_seq_point) == true);
    assert(tok_match(&ts, sym_right_brace) == true);

    /* 5. EOF token */
    tok = tok_peek(&ts, 0);
    assert(tok != NULL && tok->sym == sym_dump);

    fclose(tmp);
    osc_context_destroy(&ctx);
    symbol_id_container_release();

    printf("Token stream unit tests passed!\n");
    return 0;
}
