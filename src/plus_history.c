#include "shellforge_plus.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static int history_fd = -1;
static char *history_path;
void sf_history_init(void) {
    const char *override = getenv("SHELLFORGE_HISTORY"), *home = getenv("HOME");
    if (override) history_path = strdup(override);
    else if (home && *home) {
        size_t length = strlen(home) + sizeof("/.shellforge_history");
        history_path = malloc(length);
        if (history_path) snprintf(history_path, length, "%s/.shellforge_history", home);
    }
    if (!history_path) { fprintf(stderr, "shellforge: history disabled (no HOME or allocation failed)\n"); return; }
    if (history_path[0] != '/') {
        fprintf(stderr, "shellforge: history disabled (path must be absolute)\n"); return;
    }
    history_fd = open(history_path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
    struct stat info;
    if (history_fd >= 0 && (fstat(history_fd, &info) < 0 || !S_ISREG(info.st_mode) || info.st_uid != getuid())) {
        close(history_fd); history_fd = -1; errno = EACCES;
    }
    if (history_fd < 0) { fprintf(stderr, "shellforge: history disabled: "); perror(history_path); }
}
void sf_history_log(const char *line) {
    if (history_fd < 0 || !line[strspn(line, " \t\r\n")]) return;
    size_t length = strlen(line), written = 0;
    while (written < length) {
        ssize_t result = write(history_fd, line + written, length - written);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) goto failed;
        written += (size_t)result;
    }
    if (!length || line[length - 1] != '\n') {
        ssize_t result;
        do { result = write(history_fd, "\n", 1); } while (result < 0 && errno == EINTR);
        if (result != 1) goto failed;
    }
    return;
failed:
    fprintf(stderr, "shellforge: history write failed; logging disabled\n"); close(history_fd); history_fd = -1;
}
int sf_history_show(void) {
    if (history_fd < 0) { fprintf(stderr, "shellforge: history is disabled\n"); return 1; }
    int fd = open(history_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) { perror("history"); return 1; }
    struct stat info;
    if (fstat(fd, &info) < 0 || !S_ISREG(info.st_mode) || info.st_uid != getuid()) {
        fprintf(stderr, "shellforge: history path is no longer a regular file owned by you\n"); close(fd); return 1;
    }
    FILE *file = fdopen(fd, "r");
    if (!file) { perror("fdopen"); close(fd); return 1; }
    char *line = NULL; size_t capacity = 0;
    while (getline(&line, &capacity, file) >= 0) fputs(line, stdout);
    int failed = ferror(file);
    if (failed) perror("history read");
    free(line); fclose(file); return failed ? 1 : 0;
}
void sf_history_close(void) { if (history_fd >= 0) close(history_fd); free(history_path); }
