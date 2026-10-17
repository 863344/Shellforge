# Shellforge

A modular C shell for the twelve-week operating systems project.
External programs, quoting, pipes, redirection, `cd`, `sigaction` signals,
background jobs, environment variables, and persistent command history are included.

## Run in Ubuntu (including Ubuntu in Windows WSL)

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

Windows users: open **Ubuntu**, not PowerShell, for these commands. If Ubuntu is
not installed, run `wsl --install -d Ubuntu` from an Administrator PowerShell,
restart if requested, then open Ubuntu and complete its username/password setup.
This changes Windows features, so perform it yourself only on a device you administer.
If WSL is restricted, use your college Linux computer or an Ubuntu virtual machine.

Try these commands inside Shellforge:

```sh
help
tokenize "one two three"
printf "Hello %s\n" "$USER"
printf "b\na\n" | sort
printf "first\n" > demo.txt
cat < demo.txt
sleep 10 &
jobs
history
exit
```

The [full guide](README.plus.md) maps each faculty week to source and tests,
explains the OS concepts, and includes a short demonstration/viva plan.
[Ubuntu CI](https://github.com/863344/Shellforge/actions) runs strict compilation,
functional and terminal/process tests, and an actual Valgrind audit.
No license has been selected. Original local project artifacts are preserved;
only this new implementation and its supporting files are published here.