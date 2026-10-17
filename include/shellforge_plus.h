#ifndef SHELLFORGE_PLUS_H
#define SHELLFORGE_PLUS_H
#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stddef.h>
#include <sys/types.h>
#define SF_LIMIT 256
#define SF_BG_LIMIT 256
#define SF_WORD_LIMIT 65536
enum sf_kind { SF_WORD, SF_PIPE, SF_INPUT, SF_OUTPUT, SF_APPEND, SF_BACKGROUND };
struct sf_token { enum sf_kind kind; char *text; };
struct sf_command { char *argv[SF_LIMIT + 1]; int argc; char *input, *output; int append; };
struct sf_pipeline {
    struct sf_token tokens[SF_LIMIT];
    struct sf_command commands[SF_LIMIT];
    int tokens_count, count, background;
};
extern int sf_interactive;
extern pid_t sf_shell_group;
extern volatile sig_atomic_t sf_interrupted;
int sf_parse(const char *line, struct sf_pipeline *pipeline);
void sf_free_pipeline(struct sf_pipeline *pipeline);
int sf_signals_init(void);
void sf_child_signals(void);
void sf_block_children(sigset_t *previous);
void sf_unblock_children(const sigset_t *previous);
int sf_background_space(int count);
void sf_track_background(const pid_t *pids, int count, pid_t group);
void sf_print_jobs(void);
void sf_shutdown_jobs(void);
int sf_run(struct sf_pipeline *pipeline);
int sf_builtin(struct sf_command *command, int *quit, int previous);
int sf_is_builtin(const char *name);
void sf_history_init(void);
void sf_history_log(const char *line);
int sf_history_show(void);
void sf_history_close(void);
#endif
