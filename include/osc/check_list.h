#ifndef __OSC_CHECK_LIST_H__
#define __OSC_CHECK_LIST_H__

struct scan_file_control;
struct object;

int check_ownership_writable(struct scan_file_control *sfc, struct object *obj);
int check_ownership_owned(struct scan_file_control *sfc, struct object *obj);
int check_ownership_dropped(struct scan_file_control *sfc, struct object *obj);

#endif /* __OSC_CHECK_LIST_H__ */
