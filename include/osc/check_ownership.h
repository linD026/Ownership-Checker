#ifndef __OSC_CHECK_OWNERSHIP_H__
#define __OSC_CHECK_OWNERSHIP_H__

#include <osc/compiler.h>
#include <osc/list.h>
#include <osc/context.h>
#include <osc/cfg.h>
#include <osc/ast.h>
#include <stdbool.h>

/* Abstract Ownership & Resource States */
enum dataflow_var_val {
    VAR_STATE_UNTRACKED = 0,
    VAR_STATE_SET, /* Allocated / initialized */
    VAR_STATE_BORROWED, /* Read-only reference */
    VAR_STATE_DROPPED, /* Released / transferred */
    VAR_STATE_ESCAPED, /* Escaped to global or caller, must disarm RAII */
};

/* Per-Variable Dataflow Fact */
struct dataflow_var_state {
    struct ast_decl *decl;
    int state; /* enum dataflow_var_val */
    struct osc_span set_span;
    struct osc_span drop_span;
    struct symbol *escaped_to;
    struct list_head node;
};

/* Dataflow State Container */
struct dataflow_state {
    struct list_head var_states; /* List of struct dataflow_var_state */
    struct osc_context *ctx;
    struct cfg_function *fn;
    bool *temp_allocated;
    size_t temp_allocated_size;
};

/* Pluggable Checker Interface */
struct osc_checker {
    const char *name;

    void (*init_function)(struct osc_context *ctx, struct cfg_function *fn,
                          struct dataflow_state *state);
    void (*transfer_instruction)(struct osc_context *ctx,
                                 struct instruction *insn,
                                 struct dataflow_state *state);
    void (*join_states)(struct dataflow_state *dst,
                        const struct dataflow_state *src);
    void (*check_block_exit)(struct osc_context *ctx, struct basic_block *bb,
                             struct dataflow_state *state);
    void (*finalize_function)(struct osc_context *ctx, struct cfg_function *fn);
};

/* Model 1: Explicit OSC Ownership Checker Pass (__mut, __brw, __clone) */
extern const struct osc_checker checker_osc_attr;
/* Model 2: Unmodified Linux Kernel Resource & Hybrid RAII Checker Pass */
extern const struct osc_checker checker_linux_resource;

/* State Management Helpers */
struct dataflow_state *dataflow_state_new(struct osc_context *ctx,
                                          struct cfg_function *fn);
struct dataflow_state *dataflow_state_clone(struct osc_context *ctx,
                                            const struct dataflow_state *src);
struct dataflow_var_state *dataflow_state_get_var(struct dataflow_state *state,
                                                  struct ast_decl *decl);
void dataflow_state_set_var(struct dataflow_state *state, struct ast_decl *decl,
                            int val, struct osc_span span);
bool dataflow_state_equal(const struct dataflow_state *s1,
                          const struct dataflow_state *s2);

/* Forward Dataflow Fixpoint Solver Driver */
int run_dataflow_analysis(struct osc_context *ctx, struct cfg_function *fn,
                          const struct osc_checker *checker);

/* General helper functions */

struct ast_decl *osc_find_decl(struct osc_context *ctx, struct symbol *sym);

/* Report Helper functions */
void osc_report_span(struct osc_context *ctx, struct osc_span span,
                     const char *msg);

/* Legacy Checkers (Preserved for single-pass parser compatibility) */
struct scan_file_control;
struct object;
int check_ownership_writable(struct scan_file_control *sfc, struct object *obj);
int check_ownership_owned(struct scan_file_control *sfc, struct object *obj);
int check_ownership_dropped(struct scan_file_control *sfc, struct object *obj);

#endif /* __OSC_CHECK_OWNERSHIP_H__ */
