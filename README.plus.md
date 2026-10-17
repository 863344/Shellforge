# Shellforge Plus

A beginner-friendly operating systems project: a small command interpreter in C.
The original project is preserved. This edition adds a separate source file,
build file, executable name, and tests. No license has been selected.

## Requirements and build

Use Linux, an Ubuntu virtual machine, or Ubuntu in Windows Subsystem for Linux
(WSL). Native Windows does not provide the POSIX `fork` and `execvp` interfaces.
You need a C compiler, Make, and the usual Linux command-line utilities.
On Ubuntu, install them with `sudo apt update` then
`sudo apt install build-essential`.

### Windows: get it running

First open **Ubuntu** from the Windows Start menu. If Ubuntu opens, run the two
package commands above, then the `cd`, build, and run commands below.
If Ubuntu is not installed, open PowerShell as Administrator and run
`wsl --install -d Ubuntu`, restart Windows if prompted, then open Ubuntu and
finish creating your Linux username/password. This is a user setup step;
this project does not install WSL or change Windows features automatically.
If WSL reports access denied in your own terminal too, ask your device
administrator to allow WSL, or use a Linux college computer/Ubuntu VM instead.
The access-denied result inside this assistant does not prove WSL is broken
in your normal Windows terminal.

From the repository directory in Linux/WSL:

```sh
make -f Makefile.plus
./shellforge-plus
```

For this Windows checkout, the WSL directory is usually:

```sh
cd /mnt/c/Users/EREN/Downloads/Shellforge/Shellforge
```

Always use `-f Makefile.plus` for this edition. It builds `shellforge-plus`;
the existing `Makefile` builds the original `shellforge` executable.
To remove only the new executable: `make -f Makefile.plus clean`.

## Try it

Enter one command per line at the `shellforge+ $` prompt:

```sh
help
pwd
ls -l
cd /tmp
printf "Hello %s\n" "operating systems"
printf "orange\napple\n" | sort
printf "first\n" > shellforge-demo.txt
printf "second\n" >> shellforge-demo.txt
cat < shellforge-demo.txt | wc -l
exit 0
```

Use your host terminal to run a batch of commands:

```sh
printf 'printf "hello\\n"\nexit\n' | ./shellforge-plus
make -f Makefile.plus test
```

The test suite creates temporary files in its own temporary directory and removes
them afterwards. A successful run prints a PASS for each check. When available,
`timeout 30 make -f Makefile.plus test` also detects a pipeline hang.

## Features and boundaries

- Runs external commands found through `PATH`, including absolute/relative paths.
- `cd [directory]` changes the shell's directory; no argument uses `HOME`.
- `exit [0-255]` exits with that status, or the previous command's status.
- `help` lists the supported syntax.
- Single/double quotes group words, including empty arguments. Adjacent quoted
  and unquoted text is joined. Backslash escapes the next character outside
  quotes. Inside double quotes it escapes backslash, double quote, dollar sign,
  and backtick; other backslashes are preserved, so `printf "hello\n"` works.
  Escaped line continuations are not supported.
- `|` connects commands; `<`, `>`, and `>>` read, replace, and append files.
  Operators need no surrounding spaces. Explicit redirection overrides a pipe
  endpoint. Duplicate input or output redirections are rejected.
- Reports syntax, file, process, and command errors; continues after errors.
- Waits for all pipeline children. The last stage determines pipeline status.
  Missing commands return 127; commands that cannot execute return 126;
  syntax errors return 2; signal termination returns 128 plus the signal number.
- Interactive Ctrl+C is ignored by the shell and restored to default in children.
  A prompt may require Enter to reappear while idle. Ctrl+D ends input.

This is a teaching shell, not a replacement for Bash. Builtins must run alone
without redirection. There is no variable expansion (`$HOME`, `$?`), wildcard or
tilde expansion, command substitution, comments, multiline quoting, `;`, `&&`,
`||`, background jobs, history, tab completion, or terminal job control.
Unsupported punctuation can be passed literally; it is not interpreted as Bash
syntax. There are at most 256 tokens per line, including operators and filenames;
longer input is rejected rather than truncated. Input must be ordinary text
without embedded NUL bytes. Do not use it to run untrusted command files.

## How the code works

Read `src/shellforge_plus.c` in this order:

1. `lex`: converts an input line into words and operators while handling quotes.
2. `parse`: groups tokens into commands with arguments and file redirections.
3. `builtin`: handles commands that need to affect the shell itself.
4. `run`: creates pipes, forks children, sets up their input/output, executes
   programs, and waits for completion.
5. `main`: repeats read, parse, execute, and free until EOF or `exit`.

`fork()` creates a child process. In the child, `execvp()` replaces the process
image with a program; it returns only on failure. The parent uses `waitpid()` to
collect the child's status and avoid zombies. `cd` runs in the parent because
changing a child's working directory cannot change the parent's directory.

A pipe is a kernel-managed byte stream with a read end and a write end.
`dup2()` connects those descriptors to standard input (0) or output (1).
`open()` supplies descriptors for redirection. Each process closes unused pipe
ends so readers can receive EOF. All pipeline stages start before the parent
waits, allowing producers and consumers to run concurrently even when buffers
fill. On partial pipeline startup failure, the parent terminates and reaps
already-started children. Allocated token strings are freed after each line.

## Five-minute demonstration / viva

1. Build the new edition and run `help`, `pwd`, and `ls`.
2. Demonstrate `cd /tmp` then `pwd`; explain why `cd` is a builtin.
3. Run a quoted `printf`, then `printf "b\na\n" | sort`.
4. Demonstrate `>`, `>>`, and `<` using the example file above.
5. Enter a nonexistent command and an unmatched quote; show recovery.
6. Run the tests from the host terminal and explain fork, exec, wait, pipes,
   descriptors, EOF, and exit status using the source functions above.

Common questions: A process is a running program with its own address space.
The shell is a user-space program that requests kernel services through system
calls. `exec` does not create another process; `fork` does. A zombie is a finished
child whose status has not been collected. Closing unused pipe write ends is
necessary for EOF. A pipeline's stages run concurrently, unlike sequential
commands entered on separate lines.

## Validation and GitHub

The included tests cover quotes, escapes, pipelines, redirection, directory
changes, errors, status propagation, recovery, and a large pipeline.
This checkout was prepared on Windows where WSL and Git Bash reported access
denied. The available native Windows compiler lacks `sys/wait.h`, which this
POSIX shell needs. A build dry run and whitespace checks completed, but
compilation and runtime tests still need to be run on Linux/WSL. The test file
is supplied for that verification; do not claim a passing run until you run it.

The intended public destination is `https://github.com/863344/Shellforge`.
The old local `origin` belongs to another account and is preserved. Do not push
these additions there. The new GitHub Actions workflow builds and tests on
Ubuntu whenever changes are published. Check its actual result on the
repository's Actions tab; a workflow file alone is not evidence of a passing run.
Review and stage only the additions, preserving the pre-existing executable changes:

```sh
git add src/shellforge_plus.c Makefile.plus README.plus.md tests/test_plus.sh .github/workflows/shellforge-plus.yml
git diff --cached
git commit -m "Add Shellforge Plus teaching shell and guide"
```

Use the new account's repository as a separate remote for future pushes.
Avoid `git add .`, which would include the existing binary changes.
