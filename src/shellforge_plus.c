#include "shellforge_plus.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    char *line = NULL;
    size_t capacity = 0;
    int status = 0, quit = 0;
    if (sf_signals_init() < 0) return 1;
    sf_history_init();
    while (!quit) {
        if (sf_interactive) { fputs("shellforge+ $ ", stdout); fflush(stdout); }
        sf_interrupted = 0;
        errno = 0;
        if (getline(&line, &capacity, stdin) < 0) {
            if (errno == EINTR && sf_interrupted) {
                clearerr(stdin); putchar('\n'); status = 130; continue;
            }
            if (ferror(stdin)) { perror("getline"); status = 1; }
            break;
        }
        sf_history_log(line);
        struct sf_pipeline pipeline = {0};
        if (sf_parse(line, &pipeline) < 0) status = 2;
        else if (pipeline.count) {
            int builtin = 0;
            for (int i = 0; i < pipeline.count; ++i) builtin |= sf_is_builtin(pipeline.commands[i].argv[0]);
            if (builtin && (pipeline.count != 1 || pipeline.background ||
                           pipeline.commands[0].input || pipeline.commands[0].output)) {
                fprintf(stderr, "shellforge: builtins require a single foreground command without redirection\n");
                status = 2;
            } else if (builtin) status = sf_builtin(&pipeline.commands[0], &quit, status);
            else status = sf_run(&pipeline);
        }
        sf_free_pipeline(&pipeline);
    }
    sf_shutdown_jobs();
    sf_history_close();
    free(line);
    return status;
}