#include <osc/parser.h>
#include <osc/compiler.h>
#include <osc/list.h>
#include <osc/debug.h>
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
};

static struct osc_data osc_data = {
    .compiler = "gcc",
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
    int name_start = 0;
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
    int opt;

    while ((opt = getopt(argc, argv, "PC:I:")) != -1) {
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
        default:
            pr_err("Usage: %s [...] -P -C <compiler> -I <directory>\n",
                   argv[0]);
            BUG_ON(1, "Invalid option(s)");
        }
    }
}

int main(int argc, char *argv[])
{
    /* Init */
    list_init(&osc_data.file_head);

    osc_getopt(&osc_data, argc, argv);
    osc_getfile(&osc_data, argc, argv);
    list_for_each (&osc_data.file_head) {
        struct file_info *fi = container_of(curr, struct file_info, node);
        print("OSC Analyzes file: %s\n", fi->full_name);
        parser(fi);
    }

    symbol_id_container_release();
    delete_files(&osc_data);

    return 0;
}
