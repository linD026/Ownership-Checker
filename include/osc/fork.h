#ifndef __OSC_FORK_H__
#define __OSC_FORK_H__

struct function;
struct function_state;
struct scan_file_control;

struct function_state *fork_function_state(struct function *func);
void switch_function_state(struct scan_file_control *sfc,
                           struct function_state *new);
void fork_and_switch_function_state(struct scan_file_control *sfc);
void restore_function_state(struct scan_file_control *sfc);
void join_function_state(struct scan_file_control *sfc);

#endif /* __OSC_FORK_H__ */
