# Shellforge Plus: project guide

This is a teaching shell for the faculty's twelve-week Shellforge project.
It is not the unrelated general OS lab syllabus. The original local `Makefile`,
`src/main.c`, `shellforge`, and `src/shellforge` remain unchanged. This edition
uses new modular sources, `Makefile.plus`, and the separate `shellforge-plus`
executable. The published repository belongs to **863344**:
https://github.com/863344/Shellforge

## Build, test, and run

Use Ubuntu/Linux or Ubuntu in Windows Subsystem for Linux (WSL). Native Windows
lacks POSIX `fork`, `execvp`, and `waitpid`. In Ubuntu:

```sh
sudo apt update
sudo apt install build-essential git python3 valgrind
git clone https://github.com/863344/Shellforge.git
cd Shellforge
make -f Makefile.plus
make -f Makefile.plus test
make -f Makefile.plus valgrind
./shellforge-plus
```

If Ubuntu is already installed, open it from Start. Otherwise, in an Administrator
PowerShell run `wsl --install -d Ubuntu`, restart if requested, open Ubuntu, and
complete Linux username/password setup. These are user setup steps; this project
does not install software or change Windows features on your device automatically.
If your administrator restricts WSL, use a college Linux computer or Ubuntu VM.
Access denied inside an assistant does not establish that WSL fails in your own
Windows terminal. The assistant's native Windows execution was restricted;
Ubuntu GitHub Actions provides the independent runtime verification.

For an existing checkout use its location instead of cloning again. Always use
`make -f Makefile.plus`; `make` alone in the preserved local project builds the
old version. `make -f Makefile.plus clean` removes only the new executable.

## Faculty compliance map

| Week | Requirement | Where to demonstrate it |
| --- | --- | --- |
| 1 | REPL, `getline`, EOF/exit | `src/shellforge_plus.c`; interactive EOF and batch exit tests |
| 2 | `strtok` tokenizer | `tokenize "one two three"` in `plus_execute.c` is a real whitespace tokenizer using `strtok`; preserved local `src/main.c` also uses it for the original argument lexer |
| 3 | `fork`, `execvp`, `waitpid` | `plus_execute.c`; execution, exit-status, failure, and wait-race tests |
| 4 | `cd`, `chdir` | Parent builtin in `plus_execute.c`; directory tests |
| 5 | `open`, `dup2` redirection | `plus_execute.c`; read, truncate, append, and missing-file tests |
| 6 | `pipe` IPC | Concurrent pipeline stages in `plus_execute.c`; large-pipeline test |
| 7 | Ctrl+C through `sigaction` | `plus_jobs.c`; real pseudo-terminal Ctrl+C, foreground, background, and EOF tests |
| 8 | Actual Valgrind leak audit | `tests/test_valgrind.py`; six audited success/error scenarios in Ubuntu CI |
| 9 | Nonblocking `&`, zombie reaping | `plus_jobs.c` and `plus_execute.c`; idle reaping, background pipeline, race, and shutdown tests |
| 10 | `getenv` environment resolution | `plus_parse.c`; `$NAME`/`${NAME}`, quoting, unset variables, and limits tests |
| 11 | Persistent `.shellforge_history` | `plus_history.c`; two-launch persistence, safe-path, and failure tests |
| 12 | Modular `.h`/`.c`, Git tags | `include/shellforge_plus.h` and five C modules; release tag is created only after a verified passing revision |

Week 2 is intentionally honest: `strtok` splits the demonstration's supplied
string on whitespace. It does not understand quotes. The production argument
lexer is custom so `printf "%s" "two words"` remains correct. This lets you
explain both the required milestone and why a fuller shell needs a richer lexer.
No historic weekly commits or backdated tags are invented.

## Source tour and OS concepts

1. `src/shellforge_plus.c`: read a line, log it, parse, execute, free memory; repeat.
2. `src/plus_parse.c`: turn characters into words/operators and construct commands.
   Expansion reads inherited values with `getenv`; dynamically allocated strings
   have a single owner and are freed even after a parsing error.
3. `src/plus_execute.c`: run parent builtins, create pipes, fork, set descriptors,
   call `execvp`, and collect foreground statuses with `waitpid`.
4. `src/plus_jobs.c`: install `sigaction` handlers and track background PIDs.
5. `src/plus_history.c`: open, append, display, and close persistent history.
6. `include/shellforge_plus.h`: shared structures, limits, and function interfaces.

`fork` creates a process with its own address space. `execvp` replaces the child
with another program and searches `PATH`; successful exec never returns.
`waitpid` collects a finished child's status, preventing zombies. `cd` must run
in the parent because a child's directory change cannot change its parent.

`pipe` creates a kernel byte stream. `dup2` connects descriptors to stdin (0) and
stdout (1); `open` supplies a file descriptor for redirection. All stages start
before waiting, so a producer can continue as a consumer drains the pipe.
Unused ends are closed, allowing readers to receive EOF. Partial startup failure
terminates and reaps already-started children instead of leaving them behind.

A pipeline gets its own process group. In an interactive terminal the parent
hands the terminal to the foreground group, then takes it back after waiting.
A small startup pipe prevents children reading the terminal before that handoff.
Ctrl+C therefore reaches foreground programs, while background groups survive.
At an idle prompt Ctrl+C interrupts `getline`, clears the input error, and
redisplays the prompt. Handlers are installed with `sigaction`, not `signal`.

The SIGCHLD handler calls only async-signal-safe `waitpid` for registered
background children, uses fixed `sig_atomic_t` arrays, and preserves `errno`.
It does not print, allocate, or reap arbitrary foreground children. SIGCHLD is
blocked while the parent changes the registry; a child that exits immediately
is registered before its pending signal is delivered. With `SA_RESTART`, an idle
shell can reap background children while still waiting in `getline`.

## Supported behavior and limits

- External commands, adjacent/single/double-quoted words, empty quoted arguments,
  backslash escaping, pipelines, `<`, `>`, and `>>`. Operators need no spaces.
- Builtins: `cd [path]`, `exit [0-255]`, `help`, `jobs`, `history`, and
  `tokenize "words"`. Builtins require a single foreground command without file
  redirection; errors explain this limitation.
- A final unquoted `&` starts an entire pipeline without waiting. `jobs` lists
  direct child PIDs/groups still running. Background stdin defaults to `/dev/null`
  unless explicitly redirected, so it cannot steal the next shell command.
- SIGCHLD reaps completed background children even while idle. On `exit` or EOF,
  active tracked background groups receive SIGTERM, get up to 200 ms to finish,
  then receive SIGKILL if needed; direct children are waited for. Jobs are not
  intended to survive shell shutdown. Programs that daemonize/detach their own
  descendants are outside this shell's job tracking.
- Ctrl+C cancels foreground commands. Ctrl+Z is deliberately disabled; stopped
  job management, `fg`, and `bg` are not implemented. Background programs needing
  interactive terminal input are unsupported.
- `$NAME` and `${NAME}` expand unquoted or inside double quotes. Single quotes
  and backslash-protected dollar signs stay literal. Unset variables become empty
  arguments. Expanded values stay one argument, even unquoted: no field splitting,
  globbing, recursive expansion, or interpreting operators inside the value.
  Names use letters/digits/underscore and cannot start with a digit.
- No variable assignment/export builtin, `$?`/positional parameters, substitution,
  wildcard/tilde expansion, comments, multiline commands, `;`, `&&`, `||`, history
  recall, tab completion, or full Bash compatibility. Unsupported punctuation
  may be passed literally. Ordinary text input only, without embedded NUL bytes.
- Up to 256 tokens per line (operators and filenames count), 65,536 bytes per
  expanded word, and 256 tracked background children. Excess input is rejected.
- Last foreground pipeline stage determines status. A successful background
  launch returns 0. Missing programs return 127, failed execution 126, syntax 2,
  and signal termination 128 plus signal number. `exit` without a number preserves
  the last status. Blank lines preserve it too.

## History and privacy

Nonblank command lines (including invalid commands and `exit`) append as typed
to `$HOME/.shellforge_history`; values are not expanded in the log. The `history`
builtin includes its own just-logged line. Changing directory does not move the
history file. Do not type secrets into a logging shell: command text is saved.

To choose a different absolute path before starting, use the host Ubuntu shell:

```sh
SHELLFORGE_HISTORY=/tmp/shellforge-demo-history ./shellforge-plus
```

New history files use mode 0600 (restricted further by umask). Symlinks, nonregular
files, files owned by another user, and relative paths are rejected. Open/write
failures warn and disable logging while the shell continues. Existing file modes
are not changed. Concurrent launches append but ordering is not guaranteed; use
separate history paths for independent demonstrations. Tests always select paths
inside temporary directories and do not touch personal history.

## Testing and actual validation

`make -f Makefile.plus test` runs the 19 original functional checks plus Python
integration tests for expansions, `strtok`, persistence, malformed input, resource
failure, nonblocking jobs, idle reaping, wait races, terminal Ctrl+C/EOF, and tty
input. The process tests inspect Linux `/proc` and use Python's standard-library
PTY support. All test-created files live in temporary directories.

`make -f Makefile.plus valgrind` performs six real Memcheck runs: success,
parsing failures, execution/file errors, expansion/word-limit errors, background
jobs/shutdown, and disabled history. It audits invalid accesses and definitely,
indirectly, or possibly lost allocations. It checks every fork process report,
not just the parent's return status. A child that fails before exec can report
inherited memory as still reachable; that is explicitly distinct from lost
memory. Valgrind and test logs in GitHub Actions are the evidence; merely having
this workflow file does not imply it passed.

The earlier 19-test run verified the first edition only. For this full faculty
edition use the latest actual Ubuntu run at:
https://github.com/863344/Shellforge/actions
Local Windows/WSL access remains restricted inside the assistant. Interactive
behavior is tested on an Ubuntu pseudo-terminal, not on your physical keyboard.

## Five-minute demo and viva

1. Build and open the new executable. Enter `help`, `pwd`, and `cd /tmp`; explain
   why `cd` changes the parent process.
2. Enter `tokenize "one two three"`; show `strtok` and contrast it with the main
   quote-aware lexer. Try `printf "%s\n" "two words"`.
3. Run `printf "b\na\n" | sort`. Explain fork, exec, wait, pipe, and descriptors.
4. Write `printf "first\n" > demo.txt`, append with `>>`, and read using
   `cat < demo.txt`. Show a missing-file error and recovery.
5. Run `sleep 20 &`, `jobs`, then `sleep 20` and press Ctrl+C. The foreground
   command stops; the background child remains in `jobs`. Explain groups/reaping.
6. Run `printf "%s\n" "$HOME"`, then `printf "%s\n" '$HOME'`. Show expansion
   versus literal text. Run `history`, exit, reopen, and show persistence.
7. Show test/Valgrind success and the validated release tag. Finish with Ctrl+D.

A process is a running program; a zombie is a finished process whose parent has
not collected its status. The kernel implements system calls and pipe buffers;
the shell is a user-space program using those services. A signal handler must
avoid ordinary printing/allocation because those operations are not signal-safe.
A memory leak is allocated memory whose ownership was lost without freeing it.
A Git tag labels a particular commit; it does not prove when earlier weeks were
completed. The final release tag labels this verified integrated implementation.

## Future updates

The public code and clone URL use account `863344`. The original local Git
configuration is preserved. For future published work, use a fresh clone of your
own repository in Ubuntu and commit there; do not blindly push the preserved
checkout's unrelated history. Do not stage local original binary modifications.
No license or personal author identity has been invented.