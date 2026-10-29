#include <osc/parser.h>
#include <osc/compiler.h>
#include <osc/list.h>
#include <osc/debug.h>
#include <osc/context.h>
#include <osc/token.h>
#include <osc/ast.h>
#include <osc/cfg.h>
#include <osc/check_ownership.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <stdlib.h>

#define MAX_DIR_LEN 100
#define MAX_COMPILER_LEN 100
#define MAX_CMD_LEN \
    (MAX_COMPILER_LEN + MAX_NR_GENERATED_NAME + MAX_DIR_LEN + 128)

struct osc_data {
    struct list_head file_head;
    unsigned int nr_file;
    char include_dir[MAX_DIR_LEN];
    char compiler[MAX_COMPILER_LEN];
    int no_preprocessor;
    enum osc_mode mode;
    int dump_ast;
    int dump_cfg;
};

static struct osc_data osc_data = {
    .compiler = "gcc",
    .mode = OSC_MODE_AUTO,
};

static void osc_preprocessor(struct osc_data *data, struct file_info *fi)
{
    char cmd[MAX_CMD_LEN] = { 0 };

    if (data->include_dir[0]) {
        snprintf(cmd, MAX_CMD_LEN, "%s -E %s -o %s -I %s -D__NOT_CHECK_OSC__",
                 data->compiler, fi->full_name, fi->generated_name,
                 data->include_dir);
        cmd[MAX_CMD_LEN - 1] = '\0';
    } else {
        snprintf(cmd, MAX_CMD_LEN, "%s -E %s -o %s -D__NOT_CHECK_OSC__",
                 data->compiler, fi->full_name, fi->generated_name);
    }
    cmd[MAX_CMD_LEN - 1] = '\0';
    WARN_ON(system(cmd), "preprocessor error: %s", cmd);
}

static void create_file(struct osc_data *restrict data, char *restrict argv)
{
    int name_start = -1;
    struct file_info *fi = malloc(sizeof(struct file_info));
    BUG_ON(!fi, "malloc");

    for (int i = 0; argv[i] != '\0'; i++) {
        if (argv[i] == '/') {
            name_start = i;
            continue;
        }
        if (argv[i] == '.' && argv[i + 1] == 'c')
            break;
    }
    strncpy(fi->full_name, argv, strlen(argv));
    fi->full_name[MAX_NR_NAME - 1] = '\0';
    strncpy(fi->name, &argv[name_start + 1], strlen(argv) - name_start);
    fi->name[MAX_NR_NAME - 1] = '\0';

    if (data->no_preprocessor) {
        strncpy(fi->generated_name, fi->full_name, MAX_NR_NAME);
        fi->generated_name[MAX_NR_GENERATED_NAME - 1] = '\0';
    } else {
        snprintf(fi->generated_name, MAX_NR_GENERATED_NAME, "generated_%s",
                 fi->name);
        fi->generated_name[MAX_NR_GENERATED_NAME - 1] = '\0';
        osc_preprocessor(data, fi);
    }

    pr_debug("fi->full_name:%s, fi->name:%s, fi->generated_name:%s\n",
             fi->full_name, fi->name, fi->generated_name);

    list_init(&fi->node);
    list_init(&fi->func_head);
    list_init(&fi->struct_head);
    fi->file = NULL;

    list_add_tail(&fi->node, &data->file_head);
}

static void delete_files(struct osc_data *data)
{
    if (data->no_preprocessor)
        return;

    list_for_each_safe (&osc_data.file_head) {
        char cmd[MAX_CMD_LEN] = { 0 };
        struct file_info *fi = container_of(curr, struct file_info, node);

        snprintf(cmd, MAX_CMD_LEN, "rm -f %s", fi->generated_name);
        cmd[MAX_CMD_LEN - 1] = '\0';
        WARN_ON(system(cmd), "delete error: %s", cmd);
        list_del(&fi->node);
        free(fi);
    }
}

static void osc_getfile(struct osc_data *data, int argc, char *argv[])
{
    for (int i = 1; i < argc; i++) {
        for (int j = 0; argv[i][j] != '\0'; j++) {
            if (argv[i][j] == '.' && argv[i][j + 1] == 'c') {
                create_file(data, argv[i]);
                break;
            }
        }
    }
}

static void osc_getopt(struct osc_data *data, int argc, char *argv[])
{
    static const struct option long_options[] = {
        { "no-preprocessor", no_argument, 0, 'P' },
        { "compiler", required_argument, 0, 'C' },
        { "include", required_argument, 0, 'I' },
        { "mode", required_argument, 0, 'm' },
        { "dump-ast", no_argument, 0, 1001 },
        { "dump-cfg", no_argument, 0, 1002 },
        { "help", no_argument, 0, 'h' },
        { 0, 0, 0, 0 }
    };
    int opt;

    while ((opt = getopt_long(argc, argv, "PC:I:m:h", long_options, NULL)) !=
           -1) {
        switch (opt) {
        case 'P':
            data->no_preprocessor = 1;
            break;
        case 'C':
            strncpy(data->compiler, optarg, strlen(optarg));
            data->compiler[MAX_COMPILER_LEN - 1] = '\0';
            break;
        case 'I':
            strncpy(data->include_dir, optarg, strlen(optarg));
            data->include_dir[MAX_DIR_LEN - 1] = '\0';
            break;
        case 'm':
            if (!strcmp(optarg, "kernel")) {
                data->mode = OSC_MODE_KERNEL;
            } else if (!strcmp(optarg, "osc")) {
                data->mode = OSC_MODE_OSC;
            } else if (!strcmp(optarg, "auto")) {
                data->mode = OSC_MODE_AUTO;
            } else {
                pr_err("Invalid mode '%s': expected auto, kernel, or osc\n",
                       optarg);
                exit(EXIT_FAILURE);
            }
            break;
        case 1001:
            data->dump_ast = 1;
            break;
        case 1002:
            data->dump_cfg = 1;
            break;
        case 'h':
        default:
            print("Usage: %s [options] <files.c>...\n", argv[0]);
            print("Options:\n");
            print("  -P, --no-preprocessor   Disable external preprocessor\n");
            print("  -C, --compiler <prog>   External preprocessor binary\n");
            print("  -I, --include <dir>     Include directory\n");
            print(
                "  -m, --mode <mode>       Verification mode: auto (default), "
                "kernel, osc\n");
            print("  --dump-ast              Pretty-print parsed C11 AST\n");
            print(
                "  --dump-cfg              Dump linearized CFG instructions\n");
            print("  --legacy                Run legacy single-pass parser "
                  "only\n");
            if (opt == 'h')
                exit(EXIT_SUCCESS);
            BUG_ON(1, "Invalid option(s)");
        }
    }
}

/**
 * type_has_osc_attrs - Check whether an AST type or its members possess OSC attributes
 * @type: Target AST type
 *
 * Return: True if type or any composite member possesses __mut, __brw, or __clone.
 */
static bool type_has_osc_attrs(struct ast_type *type)
{
    struct ast_decl *m = NULL;

    if (!type)
        return false;

    if (type->osc_attrs & (ATTR_FLAGS_MUT | ATTR_FLAGS_BRW | ATTR_FLAGS_CLONE))
        return true;

    list_for_each_entry (m, &type->members, node) {
        if (m->type && type_has_osc_attrs(m->type))
            return true;
    }

    return false;
}

/**
 * tu_has_osc_attrs - Check whether translation unit contains OSC ownership attributes
 * @tu: Target AST translation unit
 *
 * Return: True if any declaration, struct member, or parameter possesses __mut, __brw, or __clone.
 */
static bool tu_has_osc_attrs(struct ast_translation_unit *tu)
{
    struct ast_decl *decl = NULL;
    struct ast_decl *param = NULL;

    if (!tu)
        return false;

    list_for_each_entry (decl, &tu->decls, node) {
        if (decl->type && type_has_osc_attrs(decl->type))
            return true;

        if (decl->type && decl->type->is_function) {
            list_for_each_entry (param, &decl->type->params, node) {
                if (param->type && type_has_osc_attrs(param->type))
                    return true;
            }
        }
    }

    return false;
}

/**
 * parser_run_pipeline - Execute the 3-phase semantic AST, CFG, and Checker pipeline
 * @fi: Source file metadata
 * @data: Global driver configuration
 *
 * Return: Number of diagnostic errors encountered, or negative errno on failure.
 */
int parser_run_pipeline(struct file_info *fi, struct osc_data *data)
{
    struct osc_context ctx;
    struct token_stream ts;
    struct ast_translation_unit *tu = NULL;
    struct list_head cfg_funcs;
    struct cfg_function *fn = NULL;
    const struct osc_checker *selected_checker = NULL;
    enum osc_mode mode;
    FILE *f = NULL;
    int errors = 0;

    if (!fi || !data)
        return -EINVAL;

    mode = data->mode;
    osc_context_init(&ctx, fi, mode);

    f = fopen(fi->generated_name, "r");
    if (!f) {
        pr_err("Failed to open generated file: %s\n", fi->generated_name);
        osc_context_destroy(&ctx);
        return -ENOENT;
    }

    token_stream_init(&ts, &ctx);
    token_stream_tokenize_file(&ts, f, fi->generated_name);
    fclose(f);

    tu = parse_translation_unit(&ts);
    if (!tu) {
        osc_context_destroy(&ctx);
        return -EINVAL;
    }

    if (data->dump_ast)
        ast_dump_translation_unit(tu, stdout);

    list_init(&cfg_funcs);
    linearize_translation_unit(&ctx, tu, &cfg_funcs);

    if (mode == OSC_MODE_OSC) {
        selected_checker = &checker_osc_attr;
    } else if (mode == OSC_MODE_KERNEL) {
        selected_checker = &checker_linux_resource;
    } else {
        if (tu_has_osc_attrs(tu))
            selected_checker = &checker_osc_attr;
        else
            selected_checker = &checker_linux_resource;
    }

    list_for_each_entry (fn, &cfg_funcs, node) {
        if (data->dump_cfg)
            cfg_dump_text(fn, stdout);

        if (selected_checker)
            run_dataflow_analysis(&ctx, fn, selected_checker);
    }

    errors = ctx.error_count;
    osc_context_destroy(&ctx);

    return errors;
}

int main(int argc, char *argv[])
{
    /* Init */
    list_init(&osc_data.file_head);

    osc_getopt(&osc_data, argc, argv);
    osc_getfile(&osc_data, argc, argv);
    // TODO: Per-file thread support with shared data?
    list_for_each (&osc_data.file_head) {
        struct file_info *fi = container_of(curr, struct file_info, node);
        print("OSC Analyzes file: %s\n", fi->full_name);
        parser_run_pipeline(fi, &osc_data);
    }

    symbol_id_container_release();
    delete_files(&osc_data);

    return 0;
}
