#include <osc/parser.h>
#include <osc/check_list.h>
#include <osc/compiler.h>
#include <osc/debug.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

/*
 * Check function:
 *
 *      type ptr id left_paren expr right_paren
 *      left_brace
 *
 *          * id = expr
 *
 *          return id
 *      right_brace
 *
 * The routine will be:
 *      1. token = get_token()
 *      2. check token type then select next path, if:
 *          - is type, check pointer, check id, goto 3
 *          - is id, check write opration, function call, goto 4
 *      3. if left_paren and is file scope, check paramter
 *         until read right_paren, and check scope
 *      4. check all the token's attr.
 */

static int decode_file_scope(struct scan_file_control *sfc)
{
    struct symbol *symbol = NULL;
    int sym = sym_dump;

    sym = get_token(sfc, &symbol);
    if (symbol == NULL)
        pr_info( "char      : %c\n", sfc->buffer[sfc->offset]);
    else
        pr_info("symbol(%2u): |%s|\n", symbol->len, symbol->name);
    return -EAGAIN;
}

static void scan_file(struct scan_file_control *sfc)
{
    for_each_line (sfc) {
        decode_action(sfc, decode_file_scope);
    }
}

int parser(struct file_info *fi)
{
    struct scan_file_control sfc = {
        .fi = fi,
        .size = MAX_BUFFER_LEN,
        .offset = 0,
        .line = 0,
    };

    fi->file = fopen(fi->name, "r");
    BUG_ON(!fi->file, "fopen");
    scan_file(&sfc);
    fclose(fi->file);

    return 0;
}
