#include <osc/parser.h>
#include <osc/compiler.h>
#include <osc/list.h>
#include <osc/debug.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <stdlib.h>

#define MAX_DIR_LEN 100

struct osc_data {
    struct list_head file_head;
    unsigned int nr_file;
    char include_dir[MAX_DIR_LEN];
};
static struct osc_data osc_data;

static void create_file(struct osc_data *restrict data, char *restrict argv)
{
    struct file_info *fi = malloc(sizeof(struct file_info));
    BUG_ON(!fi, "malloc");

    strncpy(fi->name, argv, strlen(argv));
    fi->name[MAX_NR_NAME - 1] = '\0';
    list_init(&fi->node);
    list_init(&fi->func_head);
    list_init(&fi->struct_head);
    fi->file = NULL;
    print("OSC Analyzes file: %s\n", fi->name);

    list_add_tail(&fi->node, &data->file_head);
}

static void osc_getopt(struct osc_data *data, int argc, char *argv[])
{
    int opt;

    while ((opt = getopt(argc, argv, "I:")) != -1) {
        switch (opt) {
        case 'I':
            strncpy(data->include_dir, optarg, strlen(optarg));
            break;
        default:
            pr_err("Usage: %s [-...] -I <directory>\n", argv[0]);
            BUG_ON(1, "Invalid option(s)");
        }
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

int main(int argc, char *argv[])
{
    /* Init */
    list_init(&osc_data.file_head);

    osc_getopt(&osc_data, argc, argv);
    osc_getfile(&osc_data, argc, argv);
    // TODO: use the gcc or clang to handle the preprocessor
    list_for_each (&osc_data.file_head) {
        struct file_info *fi = container_of(curr, struct file_info, node);
        parser(fi);
    }

    symbol_id_container_release();

    return 0;
}
