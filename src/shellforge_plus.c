#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

/* Explicit limits keep the parser easy to follow. Never silently truncate. */
#define LIMIT 256
enum kind { WORD, PIPE, INPUT, OUTPUT, APPEND };
struct token { enum kind kind; char *text; };
struct command { char *argv[LIMIT + 1]; int argc; char *input, *output; int append; };

static int lex(char *line, struct token *tokens) {
    int n = 0;
    char *p = line;
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n') ++p;
        if (!*p) break;
        if (n == LIMIT) { fprintf(stderr, "shellforge: too many tokens\n"); goto fail; }
        tokens[n].text = NULL;
        if (strchr("|<>", *p)) {
            tokens[n].kind = *p == '|' ? PIPE : *p == '<' ? INPUT : OUTPUT;
            if (*p++ == '>' && *p == '>') { tokens[n].kind = APPEND; ++p; }
            ++n;
            continue;
        }
        char *word = malloc(strlen(p) + 1), *out;
        if (!word) { perror("malloc"); goto fail; }
        out = word;
        char quote = 0;
        while (*p && (quote || !strchr(" \t\n|<>", *p))) {
            if (!quote && (*p == '\'' || *p == '"')) { quote = *p++; continue; }
            if (quote && *p == quote) { quote = 0; ++p; continue; }
            /* In double quotes, preserve e.g. \\n for programs such as printf. */
            if (*p == '\\' && quote != '\'' &&
                (quote != '"' || (p[1] && strchr("\\\"$`\n", p[1])))) {
                ++p;
                if (!*p || *p == '\n') {
                    fprintf(stderr, "shellforge: unfinished escape\n"); free(word); goto fail;
                }
            }
            *out++ = *p++;
        }
        if (quote) { fprintf(stderr, "shellforge: unmatched quote\n"); free(word); goto fail; }
        *out = '\0';
        tokens[n++] = (struct token){ WORD, word };
    }
    return n;
fail:
    while (n) free(tokens[--n].text);
    return -1;
}

static int parse(struct token *t, int n, struct command *cmd) {
    int c = 0;
    for (int i = 0; i < n; ++i) {
        if (t[i].kind == WORD) cmd[c].argv[cmd[c].argc++] = t[i].text;
        else if (t[i].kind == PIPE) {
            if (!cmd[c].argc) goto syntax;
            ++c;
        } else {
            enum kind k = t[i].kind;
            if (++i == n || t[i].kind != WORD) goto syntax;
            char **path = k == INPUT ? &cmd[c].input : &cmd[c].output;
            if (*path) { fprintf(stderr, "shellforge: duplicate redirection\n"); return -1; }
            *path = t[i].text;
            if (k != INPUT) cmd[c].append = k == APPEND;
        }
    }
    if (!cmd[c].argc) goto syntax;
    return c + 1;
syntax:
    fprintf(stderr, "shellforge: expected command or redirection filename\n");
    return -1;
}

static int is_builtin(const char *s) {
    return !strcmp(s, "cd") || !strcmp(s, "exit") || !strcmp(s, "help");
}

static int builtin(struct command *c, int *quit, int previous) {
    char *name = c->argv[0];
    if (!strcmp(name, "help")) {
        if (c->argc != 1) { fprintf(stderr, "usage: help\n"); return 2; }
        puts("Shellforge Plus: cd [directory], exit [0-255], help\n"
             "External commands support quotes, escapes, |, <, >, >>.\n"
             "Builtins run alone without redirection. No expansion or job control.");
        return 0;
    }
    if (!strcmp(name, "cd")) {
        if (c->argc > 2) { fprintf(stderr, "usage: cd [directory]\n"); return 2; }
        char *path = c->argc == 2 ? c->argv[1] : getenv("HOME");
        if (!path) { fprintf(stderr, "shellforge: HOME is not set\n"); return 1; }
        if (chdir(path) < 0) { perror("cd"); return 1; }
        return 0;
    }
    if (c->argc > 2) { fprintf(stderr, "usage: exit [0-255]\n"); return 2; }
    if (c->argc == 2) {
        char *end;
        errno = 0;
        long value = strtol(c->argv[1], &end, 10);
        if (errno || !*c->argv[1] || *end || value < 0 || value > 255) {
            fprintf(stderr, "shellforge: exit expects an integer from 0 to 255\n"); return 2;
        }
        previous = (int)value;
    }
    *quit = 1;
    return previous;
}

static int redirect(const char *path, int target, int flags) {
    if (!path) return 0;
    int fd = open(path, flags, 0666);
    if (fd < 0) { perror(path); return -1; }
    if (dup2(fd, target) < 0) { perror("dup2"); close(fd); return -1; }
    if (fd != target) close(fd);
    return 0;
}

static int run(struct command *cmd, int count) {
    pid_t children[LIMIT];
    int started = 0, previous = -1, failed = 0, result = 1;
    /* Start every pipeline stage before waiting: large producers must not deadlock. */
    for (int i = 0; i < count; ++i) {
        int next[2] = {-1, -1};
        if (i + 1 < count && pipe(next) < 0) { perror("pipe"); failed = 1; break; }
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            if (next[0] >= 0) { close(next[0]); close(next[1]); }
            failed = 1; break;
        }
        if (pid == 0) {
            signal(SIGINT, SIG_DFL);
            signal(SIGQUIT, SIG_DFL);
            if ((previous >= 0 && dup2(previous, STDIN_FILENO) < 0) ||
                (next[1] >= 0 && dup2(next[1], STDOUT_FILENO) < 0)) {
                perror("dup2"); _exit(1);
            }
            if (previous >= 0) close(previous);
            if (next[0] >= 0) { close(next[0]); close(next[1]); }
            if (redirect(cmd[i].input, STDIN_FILENO, O_RDONLY) < 0 ||
                redirect(cmd[i].output, STDOUT_FILENO,
                         O_WRONLY | O_CREAT | (cmd[i].append ? O_APPEND : O_TRUNC)) < 0) _exit(1);
            execvp(cmd[i].argv[0], cmd[i].argv);
            int code = errno == ENOENT ? 127 : 126;
            perror(cmd[i].argv[0]);
            _exit(code);
        }
        children[started++] = pid;
        if (previous >= 0) close(previous);
        if (next[1] >= 0) close(next[1]);
        previous = next[0];
    }
    if (previous >= 0) close(previous);
    if (failed) for (int i = 0; i < started; ++i) kill(children[i], SIGKILL);
    for (int i = 0; i < started; ++i) {
        int status;
        pid_t waited;
        do { waited = waitpid(children[i], &status, 0); } while (waited < 0 && errno == EINTR);
        if (waited < 0) { perror("waitpid"); failed = 1; }
        else if (i == started - 1)
            result = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
    }
    return failed ? 1 : result;
}

int main(void) {
    char *line = NULL;
    size_t capacity = 0;
    int status = 0, quit = 0;
    int interactive = isatty(STDIN_FILENO);
    if (interactive) { signal(SIGINT, SIG_IGN); signal(SIGQUIT, SIG_IGN); }
    while (!quit) {
        if (interactive) { fputs("shellforge+ $ ", stdout); fflush(stdout); }
        if (getline(&line, &capacity, stdin) < 0) {
            if (ferror(stdin)) { perror("getline"); status = 1; }
            break;
        }
        struct token tokens[LIMIT] = {0};
        struct command commands[LIMIT] = {0};
        int n = lex(line, tokens);
        if (n < 0) { status = 2; continue; }
        if (n > 0) {
            int count = parse(tokens, n, commands), has_builtin = 0;
            if (count > 0) {
                for (int i = 0; i < count; ++i) has_builtin |= is_builtin(commands[i].argv[0]);
                if (has_builtin && (count != 1 || commands[0].input || commands[0].output)) {
                    fprintf(stderr, "shellforge: builtins require a single command without redirection\n"); status = 2;
                } else if (has_builtin) status = builtin(&commands[0], &quit, status);
                else status = run(commands, count);
            } else status = 2;
        }
        for (int i = 0; i < n; ++i) free(tokens[i].text);
    }
    free(line);
    return status;
}
