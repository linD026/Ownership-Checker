#ifndef __OSC_PARSER_H__
#define __OSC_PARSER_H__

#include <osc/list.h>
#include <stdio.h>

#define MAX_NR_NAME 32

typedef struct statement_struct {
    /* For scope struct */
    struct list_head node;
} statement_t;

typedef struct scope_struct {
    /* For function struct */
    struct list_head node;
    struct list_head statement_head;
    unsigned int nr_statement;
} scope_t;

typedef struct function_struct {
    char name[MAX_NR_NAME];
    /* For file struct */
    struct list_head node;
    struct list_head scope_head;
    unsigned int nr_scope;
} function_t;

typedef struct file_struct {
    char name[MAX_NR_NAME];
    /* For osc data struct in src/osc.c */
    struct list_head node;
    struct list_head function_head;
    unsigned int nr_function;
    FILE *file;
} file_t;

int parser(struct list_head *file_head);

#endif /* __OSC_PARSER_H__ */
