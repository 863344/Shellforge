#include "shellforge_plus.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
int sf_interactive;
pid_t sf_shell_group;
volatile sig_atomic_t sf_interrupted;
/* The handler reaps only registered background PIDs, never foreground children.
 * sig_atomic_t PID storage targets Linux, whose pid_t and sig_atomic_t are int. */
static volatile sig_atomic_t background_pids[SF_BG_LIMIT];
static volatile sig_atomic_t background_groups[SF_BG_LIMIT];
static void reap_background(int signal_number) {
    (void)signal_number;
    int saved_errno = errno;
    for (int i = 0; i < SF_BG_LIMIT; ++i) {
        pid_t pid = (pid_t)background_pids[i];
        if (pid > 0) {
            pid_t result = waitpid(pid, NULL, WNOHANG);
            if (result == pid || (result < 0 && errno == ECHILD)) background_pids[i] = 0;
        }
    }
    errno = saved_errno;
}
static void interrupt_prompt(int signal_number) { (void)signal_number; sf_interrupted = 1; }
static int install(int number, void (*handler)(int), int flags) {
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handler; action.sa_flags = flags;
    sigemptyset(&action.sa_mask);
    return sigaction(number, &action, NULL);
}
int sf_signals_init(void) {
    sf_interactive = isatty(STDIN_FILENO); sf_shell_group = getpgrp();
    if (sf_interactive) {
        if (tcgetpgrp(STDIN_FILENO) != sf_shell_group) {
            fprintf(stderr, "shellforge: start this shell in the foreground\n"); return -1;
        }
        if (install(SIGTTOU, SIG_IGN, 0) < 0 || install(SIGTTIN, SIG_IGN, 0) < 0 ||
            install(SIGTSTP, SIG_IGN, 0) < 0 || install(SIGQUIT, SIG_IGN, 0) < 0 ||
            install(SIGINT, interrupt_prompt, 0) < 0) { perror("sigaction"); return -1; }
    }
    if (install(SIGCHLD, reap_background, SA_RESTART | SA_NOCLDSTOP) < 0) { perror("sigaction"); return -1; }
    return 0;
}
void sf_child_signals(void) {
    install(SIGINT, SIG_DFL, 0); install(SIGQUIT, SIG_DFL, 0);
    install(SIGCHLD, SIG_DFL, 0); install(SIGTTIN, SIG_DFL, 0); install(SIGTTOU, SIG_DFL, 0);
    /* Stopped-job management (fg/bg) is outside this project. */
    install(SIGTSTP, SIG_IGN, 0);
}
void sf_block_children(sigset_t *previous) {
    sigset_t mask; sigemptyset(&mask); sigaddset(&mask, SIGCHLD);
    sigprocmask(SIG_BLOCK, &mask, previous);
}
void sf_unblock_children(const sigset_t *previous) { sigprocmask(SIG_SETMASK, previous, NULL); }
int sf_background_space(int count) {
    reap_background(0);
    int free_slots = 0;
    for (int i = 0; i < SF_BG_LIMIT; ++i) free_slots += background_pids[i] == 0;
    return free_slots >= count;
}
void sf_track_background(const pid_t *pids, int count, pid_t group) {
    int child = 0;
    for (int i = 0; i < SF_BG_LIMIT && child < count; ++i) {
        if (!background_pids[i]) {
            background_groups[i] = (sig_atomic_t)group;
            background_pids[i] = (sig_atomic_t)pids[child++];
        }
    }
    fprintf(stderr, "[background %ld]\n", (long)group);
}
void sf_print_jobs(void) {
    sigset_t previous; sf_block_children(&previous); reap_background(0);
    for (int i = 0; i < SF_BG_LIMIT; ++i)
        if (background_pids[i]) printf("pid=%ld group=%ld running\n", (long)background_pids[i], (long)background_groups[i]);
    sf_unblock_children(&previous);
}
void sf_shutdown_jobs(void) {
    sigset_t previous; sf_block_children(&previous); reap_background(0);
    for (int i = 0; i < SF_BG_LIMIT; ++i)
        if (background_pids[i]) kill(-(pid_t)background_groups[i], SIGTERM);
    for (int attempt = 0; attempt < 20; ++attempt) {
        int active = 0; reap_background(0);
        for (int i = 0; i < SF_BG_LIMIT; ++i) active |= background_pids[i] != 0;
        if (!active) break;
        struct timespec delay = {0, 10000000}; nanosleep(&delay, NULL);
    }
    for (int i = 0; i < SF_BG_LIMIT; ++i)
        if (background_pids[i]) kill(-(pid_t)background_groups[i], SIGKILL);
    for (int i = 0; i < SF_BG_LIMIT; ++i) {
        pid_t pid = (pid_t)background_pids[i];
        if (pid > 0) { while (waitpid(pid, NULL, 0) < 0 && errno == EINTR) {} background_pids[i] = 0; }
    }
    sf_unblock_children(&previous);
}
