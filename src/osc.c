#include <osc/parser.h>

#define MAX_NR_ROOT_DIR 100

struct osc_data {
    struct list_head file_head;
    unsigned int nr_file;
    char root_dir[MAX_NR_ROOT_DIR];
};

static struct osc_data *osc_data_init(int argc, char *argv[])
{

}

int main(int argc, char *argv[])
{
    return 0;
}
