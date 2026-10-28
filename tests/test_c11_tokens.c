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
    struct scan_file_control sfc;
    struct symbol *sym = NULL;
    int token = 0;
    FILE *tmp = NULL;
    const char *code =
        "_Static_assert _Generic _Atomic _Thread_local _Alignas _Alignof inline asm __attribute__ != <= >= << >>;\n";

    tmp = tmpfile();
    assert(tmp != NULL);
    fputs(code, tmp);
    rewind(tmp);

    memset(&fi, 0, sizeof(fi));
    fi.file = tmp;
    strcpy(fi.name, "test_tokens.c");
    strcpy(fi.generated_name, "test_tokens.c");

    osc_context_init(&ctx, &fi, OSC_MODE_AUTO);
    memset(&sfc, 0, sizeof(sfc));
    sfc.fi = &fi;
    sfc.ctx = &ctx;
    sfc.size = MAX_BUFFER_LEN;
    list_init(&sfc.peak_head);
    list_init(&sfc.typedef_info_head);

    token_init(&sfc);

    /* 1. _Static_assert */
    token = get_token(&sfc, &sym);
    assert(token == sym__Static_assert);

    /* 2. _Generic */
    token = get_token(&sfc, &sym);
    assert(token == sym__Generic);

    /* 3. _Atomic */
    token = get_token(&sfc, &sym);
    assert(token == sym__Atomic);

    /* 4. _Thread_local */
    token = get_token(&sfc, &sym);
    assert(token == sym__Thread_local);

    /* 5. _Alignas */
    token = get_token(&sfc, &sym);
    assert(token == sym__Alignas);

    /* 6. _Alignof */
    token = get_token(&sfc, &sym);
    assert(token == sym__Alignof);

    /* 7. inline */
    token = get_token(&sfc, &sym);
    assert(token == sym_inline);

    /* 8. asm */
    token = get_token(&sfc, &sym);
    assert(token == sym_asm);

    /* 9. __attribute__ */
    token = get_token(&sfc, &sym);
    assert(token == sym___attribute__);

    /* 10. != */
    token = get_token(&sfc, &sym);
    assert(token == sym_not_equal);

    /* 11. <= */
    token = get_token(&sfc, &sym);
    assert(token == sym_le);

    /* 12. >= */
    token = get_token(&sfc, &sym);
    assert(token == sym_ge);

    /* 13. << */
    token = get_token(&sfc, &sym);
    assert(token == sym_bit_left);

    /* 14. >> */
    token = get_token(&sfc, &sym);
    assert(token == sym_bit_right);

    /* 15. ; */
    token = get_token(&sfc, &sym);
    assert(token == sym_seq_point);

    fclose(tmp);
    osc_context_destroy(&ctx);
    symbol_id_container_release();

    printf("C11 and GNU token scanning test passed!\n");
    return 0;
}
