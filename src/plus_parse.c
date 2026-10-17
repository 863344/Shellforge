#include "shellforge_plus.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct word { char *data; size_t length, capacity; };
static int append(struct word *word, const char *text, size_t length) {
    if (length > SF_WORD_LIMIT - word->length) {
        fprintf(stderr, "shellforge: expanded word exceeds %d bytes\n", SF_WORD_LIMIT); return -1;
    }
    size_t needed = word->length + length + 1;
    if (needed > word->capacity) {
        size_t capacity = needed * 2;
        char *grown = realloc(word->data, capacity);
        if (!grown) { perror("realloc"); return -1; }
        word->data = grown; word->capacity = capacity;
    }
    memcpy(word->data + word->length, text, length);
    word->length += length;
    word->data[word->length] = '\0';
    return 0;
}
static int variable(struct word *word, const char **cursor) {
    const char *p = *cursor + 1, *start;
    int braces = *p == '{';
    if (braces) ++p;
    start = p;
    if (!(isalpha((unsigned char)*p) || *p == '_')) {
        if (braces) { fprintf(stderr, "shellforge: invalid variable name\n"); return -1; }
        ++*cursor; return append(word, "$", 1);
    }
    while (isalnum((unsigned char)*p) || *p == '_') ++p;
    if (braces && *p != '}') { fprintf(stderr, "shellforge: expected } after variable name\n"); return -1; }
    size_t length = (size_t)(p - start);
    char *name = malloc(length + 1);
    if (!name) { perror("malloc"); return -1; }
    memcpy(name, start, length); name[length] = '\0';
    const char *value = getenv(name);
    free(name); *cursor = p + braces;
    return value ? append(word, value, strlen(value)) : 0;
}
static int lex(const char *line, struct sf_pipeline *pipeline) {
    const char *p = line;
    while (*p) {
        while (*p && strchr(" \t\r\n", *p)) ++p;
        if (!*p) break;
        int n = pipeline->tokens_count;
        if (n == SF_LIMIT) { fprintf(stderr, "shellforge: too many tokens\n"); return -1; }
        struct sf_token *token = &pipeline->tokens[n];
        if (strchr("|<>&", *p)) {
            token->kind = *p == '|' ? SF_PIPE : *p == '<' ? SF_INPUT : *p == '&' ? SF_BACKGROUND : SF_OUTPUT;
            if (*p++ == '>' && *p == '>') { token->kind = SF_APPEND; ++p; }
            ++pipeline->tokens_count; continue;
        }
        struct word word = {0};
        char quote = 0;
        if (append(&word, "", 0) < 0) return -1;
        while (*p && (quote || !strchr(" \t\r\n|<>&", *p))) {
            if (!quote && (*p == '\'' || *p == '"')) { quote = *p++; continue; }
            if (quote && *p == quote) { quote = 0; ++p; continue; }
            if (*p == '$' && quote != '\'') {
                if (variable(&word, &p) < 0) goto bad_word;
                continue;
            }
            if (*p == '\\' && quote != '\'' && (quote != '"' || (p[1] && strchr("\\\"$`\n", p[1])))) {
                ++p;
                if (!*p || *p == '\n') { fprintf(stderr, "shellforge: unfinished escape\n"); goto bad_word; }
            }
            if (append(&word, p++, 1) < 0) goto bad_word;
        }
        if (quote) { fprintf(stderr, "shellforge: unmatched quote\n"); goto bad_word; }
        *token = (struct sf_token){SF_WORD, word.data};
        ++pipeline->tokens_count; continue;
bad_word:
        free(word.data); return -1;
    }
    return 0;
}
int sf_parse(const char *line, struct sf_pipeline *pipeline) {
    if (lex(line, pipeline) < 0) return -1;
    int n = pipeline->tokens_count, c = 0;
    if (!n) return 0;
    struct sf_token *tokens = pipeline->tokens;
    if (tokens[n - 1].kind == SF_BACKGROUND) { pipeline->background = 1; --n; }
    for (int i = 0; i < n; ++i) {
        struct sf_command *command = &pipeline->commands[c];
        if (tokens[i].kind == SF_WORD) command->argv[command->argc++] = tokens[i].text;
        else if (tokens[i].kind == SF_PIPE) { if (!command->argc) goto syntax; ++c; }
        else if (tokens[i].kind == SF_BACKGROUND) goto syntax;
        else {
            enum sf_kind kind = tokens[i].kind;
            if (++i == n || tokens[i].kind != SF_WORD) goto syntax;
            char **path = kind == SF_INPUT ? &command->input : &command->output;
            if (*path) { fprintf(stderr, "shellforge: duplicate redirection\n"); return -1; }
            *path = tokens[i].text;
            if (kind != SF_INPUT) command->append = kind == SF_APPEND;
        }
    }
    if (!pipeline->commands[c].argc) goto syntax;
    pipeline->count = c + 1; return 0;
syntax:
    fprintf(stderr, "shellforge: expected command, final &, or redirection filename\n"); return -1;
}
void sf_free_pipeline(struct sf_pipeline *pipeline) {
    for (int i = 0; i < pipeline->tokens_count; ++i) free(pipeline->tokens[i].text);
}
