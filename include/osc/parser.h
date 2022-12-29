#ifndef __OSC_PARSER_H__
#define __OSC_PARSER_H__

#include <osc/list.h>
#include <stdio.h>

#define MAX_NR_NAME 80

struct file_info {
    char name[MAX_NR_NAME];
    /* For osc data struct in src/osc.c */
    struct list_head node;
    FILE *file;
};

int parser(struct file_info *fi);

#endif /* __OSC_PARSER_H__ */
