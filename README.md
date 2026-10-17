# Shellforge

A small POSIX shell in C for learning operating systems: processes, pipes,
file descriptors, redirection, and exit status.

Shellforge Plus supports external commands, `cd`, `exit`, `help`, single/double
quotes, pipelines (`|`), and file redirection (`<`, `>`, `>>`).

## Run on Ubuntu or Windows with Ubuntu in WSL

```sh
sudo apt update
sudo apt install build-essential git
git clone https://github.com/863344/Shellforge.git
cd Shellforge
make -f Makefile.plus
make -f Makefile.plus test
./shellforge-plus
```

Try `help`, `ls -l`, `printf "b\na\n" | sort`, then `exit`.
Native Windows cannot run this POSIX program directly. Open Ubuntu for these
commands. See the [complete guide](README.plus.md) for WSL setup, examples,
limitations, the source walkthrough, and a viva demonstration.

The [Linux workflow](.github/workflows/shellforge-plus.yml) builds with strict
compiler warnings and runs the functional tests on Ubuntu. Check the repository's
Actions tab for the actual result. No license has been selected.
