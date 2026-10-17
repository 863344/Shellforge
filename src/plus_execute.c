#include "shellforge_plus.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
int sf_is_builtin(const char *name) {
    return !strcmp(name, "cd") || !strcmp(name, "exit") || !strcmp(name, "help") ||
           !strcmp(name, "jobs") || !strcmp(name, "history") || !strcmp(name, "tokenize");
}
int sf_builtin(struct sf_command *command, int *quit, int previous) {
    char *name = command->argv[0];
    if (!strcmp(name, "cd")) {
        if (command->argc > 2) { fprintf(stderr, "usage: cd [directory]\n"); return 2; }
        const char *path = command->argc == 2 ? command->argv[1] : getenv("HOME");
        if (!path) { fprintf(stderr, "shellforge: HOME is not set\n"); return 1; }
        if (chdir(path) < 0) { perror("cd"); return 1; } return 0;
    }
    if (!strcmp(name, "exit")) {
        if (command->argc > 2) { fprintf(stderr, "usage: exit [0-255]\n"); return 2; }
        if (command->argc == 2) {
            char *end; errno = 0;
            long value = strtol(command->argv[1], &end, 10);
            if (errno || !*command->argv[1] || *end || value < 0 || value > 255) {
                fprintf(stderr, "shellforge: exit expects 0 through 255\n"); return 2;
            }
            previous = (int)value;
        }
        *quit = 1; return previous;
    }
    /* Week 2: strtok destructively splits a private copy; the main lexer must
     * remain quote-aware because strtok alone cannot preserve shell quotes. */
    if (!strcmp(name, "tokenize")) {
        if (command->argc != 2) { fprintf(stderr, "usage: tokenize \"words to split\"\n"); return 2; }
        char *copy = strdup(command->argv[1]);
        if (!copy) { perror("strdup"); return 1; }
        int number = 0;
        for (char *token = strtok(copy, " \t\r\n"); token; token = strtok(NULL, " \t\r\n")) printf("%d: %s\n", ++number, token);
        free(copy); return 0;
    }
    if (command->argc != 1) { fprintf(stderr, "shellforge: this builtin takes no arguments\n"); return 2; }
    if (!strcmp(name, "jobs")) { sf_print_jobs(); return 0; }
    if (!strcmp(name, "history")) return sf_history_show();
    puts("Shellforge Plus: cd [directory], exit [0-255], jobs, history, help\n"
         "tokenize \"words\" demonstrates strtok. External commands support\n"
         "quotes, $NAME/${NAME}, |, <, >, >> and a final & for background work.\n"
         "Builtins run alone without redirection or &. Ctrl+Z/fg/bg are not supported.");
    return 0;
}
static int redirect(const char *path, int target, int flags) {
    if (!path) return 0;
    int fd = open(path, flags, 0666);
    if (fd < 0) { perror(path); return -1; }
    if (dup2(fd, target) < 0) { perror("dup2"); close(fd); return -1; }
    if (fd != target) close(fd);
    return 0;
}
static int group_child(pid_t pid, pid_t group) {
    if (setpgid(pid, group) == 0 || errno == EACCES || errno == ESRCH) return 0;
    perror("setpgid"); return -1;
}
int sf_run(struct sf_pipeline *pipeline) {
    pid_t children[SF_LIMIT], group = 0;
    int started = 0, previous_input = -1, failed = 0, result = 1, gate[2] = {-1, -1};
    sigset_t previous_mask; sf_block_children(&previous_mask);
    if (pipeline->background && !sf_background_space(pipeline->count)) {
        fprintf(stderr, "shellforge: background child limit reached\n"); sf_unblock_children(&previous_mask); return 1;
    }
    if (sf_interactive && !pipeline->background && pipe(gate) < 0) { perror("pipe"); sf_unblock_children(&previous_mask); return 1; }
    fflush(NULL);
    for (int i = 0; i < pipeline->count; ++i) {
        int next[2] = {-1, -1};
        if (i + 1 < pipeline->count && pipe(next) < 0) { perror("pipe"); failed = 1; break; }
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork"); if (next[0] >= 0) { close(next[0]); close(next[1]); } failed = 1; break;
        }
        if (pid == 0) {
            if (setpgid(0, group ? group : 0) < 0) { perror("setpgid"); _exit(1); }
            sf_child_signals(); sf_unblock_children(&previous_mask);
            sf_history_close();
            if (gate[0] >= 0) {
                close(gate[1]); char byte;
                while (read(gate[0], &byte, 1) < 0 && errno == EINTR) {} close(gate[0]);
            }
            if ((previous_input >= 0 && dup2(previous_input, STDIN_FILENO) < 0) ||
                (next[1] >= 0 && dup2(next[1], STDOUT_FILENO) < 0)) { perror("dup2"); _exit(1); }
            if (previous_input >= 0) close(previous_input);
            if (next[0] >= 0) { close(next[0]); close(next[1]); }
            struct sf_command *command = &pipeline->commands[i];
            if (pipeline->background && i == 0 && !command->input && redirect("/dev/null", STDIN_FILENO, O_RDONLY) < 0) _exit(1);
            if (redirect(command->input, STDIN_FILENO, O_RDONLY) < 0 || redirect(command->output, STDOUT_FILENO,
                O_WRONLY | O_CREAT | (command->append ? O_APPEND : O_TRUNC)) < 0) _exit(1);
            execvp(command->argv[0], command->argv);
            int code = errno == ENOENT ? 127 : 126; perror(command->argv[0]); _exit(code);
        }
        if (!group) group = pid;
        children[started++] = pid;
        if (group_child(pid, group) < 0) failed = 1;
        if (previous_input >= 0) close(previous_input);
        if (next[1] >= 0) close(next[1]);
        previous_input = next[0]; if (failed) break;
    }
    if (previous_input >= 0) close(previous_input);
    if (!failed && sf_interactive && !pipeline->background && tcsetpgrp(STDIN_FILENO, group) < 0) { perror("tcsetpgrp"); failed = 1; }
    if (gate[0] >= 0) { close(gate[0]); close(gate[1]); }
    if (!failed && pipeline->background) {
        sf_track_background(children, started, group); sf_unblock_children(&previous_mask); return 0;
    }
    if (failed) {
        if (group > 0) kill(-group, SIGKILL);
        for (int i = 0; i < started; ++i) kill(children[i], SIGKILL);
    }
    sf_unblock_children(&previous_mask);
    for (int i = 0; i < started; ++i) {
        int status; pid_t waited;
        do { waited = waitpid(children[i], &status, 0); } while (waited < 0 && errno == EINTR);
        if (waited < 0) { perror("waitpid"); failed = 1; }
        else if (i == started - 1) result = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    }
    if (sf_interactive && tcsetpgrp(STDIN_FILENO, sf_shell_group) < 0) { perror("tcsetpgrp"); failed = 1; }
    return failed ? 1 : result;
}
