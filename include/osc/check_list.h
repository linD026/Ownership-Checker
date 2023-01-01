#ifndef __OSC_CHECK_LIST_H__
#define __OSC_CHECK_LIST_H__

struct scan_file_control;
struct fsobject_struct;
struct bsobject_struct;

void bad(struct scan_file_control *sfc, const char *warning);
void bad_fsobject(struct fsobject_struct *fso);

int check_ownership(void);

int check_func_args_write(struct scan_file_control *sfc,
                          struct bsobject_struct *bso);

#endif /* __OSC_CHECK_LIST_H__ */
