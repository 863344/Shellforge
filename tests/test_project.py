"""Linux integration checks; no third-party Python packages are needed."""
import errno
import os
from pathlib import Path
import pty
import re
import resource
import select
import signal
import subprocess
import tempfile
import time
import unittest

SHELL = str(Path(__file__).resolve().parents[1] / "shellforge-plus")


class Conversation:
    def __init__(self, env, cwd, terminal=False):
        self.terminal = terminal
        self.buffer = b""
        if terminal:
            self.pid, self.fd = pty.fork()
            if self.pid == 0:
                os.chdir(cwd)
                os.execve(SHELL, [SHELL], env)
        else:
            self.process = subprocess.Popen([SHELL], stdin=subprocess.PIPE,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env, cwd=cwd)
            self.pid = self.process.pid
            self.fd = self.process.stdout.fileno()

    def send(self, text):
        data = text.encode() if isinstance(text, str) else text
        if self.terminal:
            os.write(self.fd, data)
        else:
            self.process.stdin.write(data)
            self.process.stdin.flush()

    def expect(self, pattern, timeout=4):
        deadline = time.monotonic() + timeout
        while True:
            match = re.search(pattern, self.buffer)
            if match:
                self.buffer = self.buffer[match.end():]
                return match
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([self.fd], [], [], remaining)[0]:
                raise AssertionError(f"Timed out waiting for {pattern!r}: {self.buffer!r}")
            try:
                block = os.read(self.fd, 65536)
            except OSError as error:
                if error.errno != errno.EIO:
                    raise
                block = b""
            if not block:
                raise AssertionError(f"Unexpected EOF waiting for {pattern!r}: {self.buffer!r}")
            self.buffer += block

    def wait(self):
        if not self.terminal:
            return self.process.wait(timeout=4)
        deadline = time.monotonic() + 4
        while time.monotonic() < deadline:
            pid, status = os.waitpid(self.pid, os.WNOHANG)
            if pid:
                self.pid = 0
                return os.waitstatus_to_exitcode(status)
            time.sleep(.01)
        raise AssertionError("Interactive shell did not exit")

    def close(self):
        if self.terminal:
            if self.pid:
                try:
                    os.kill(self.pid, signal.SIGKILL)
                    os.waitpid(self.pid, 0)
                except ProcessLookupError:
                    pass
            os.close(self.fd)
        else:
            if self.process.poll() is None:
                self.process.kill()
            self.process.communicate(timeout=4)


class ProjectTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.directory = Path(self.temp.name)
        self.env = dict(os.environ, HOME=self.temp.name,
            SHELLFORGE_HISTORY=str(self.directory / ".shellforge_history"), SF_TEST="two words|literal")
        self.sessions = []

    def tearDown(self):
        for session in self.sessions:
            session.close()
        self.temp.cleanup()

    def run_shell(self, script, **kwargs):
        return subprocess.run([SHELL], input=script, text=True, capture_output=True,
            env=kwargs.pop("env", self.env), cwd=self.directory, timeout=6, **kwargs)

    def session(self, terminal=False):
        session = Conversation(self.env, self.temp.name, terminal)
        self.sessions.append(session)
        if terminal:
            session.expect(rb"shellforge\+ \$ ")
        return session

    def test_environment_and_quote_rules(self):
        result = self.run_shell('printf "<%s><%s><%s><%s>" $SF_TEST "${SF_TEST}" \'$SF_TEST\' "$SF_UNSET_492"\n')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, '<two words|literal><two words|literal><$SF_TEST><>')

    def test_escaped_dollar_and_adjacent_fragments(self):
        result = self.run_shell('printf "%s" pre"${SF_TEST}"post\\$SF_TEST\n')
        self.assertEqual(result.stdout, 'pretwo words|literalpost$SF_TEST')

    def test_invalid_expansion_and_limits(self):
        for text in ['printf "${BAD"', 'printf ${1}', 'printf ' + 'x ' * 257,
                     'printf "' + 'x' * 65537 + '"']:
            with self.subTest(text=text[:40]):
                self.assertEqual(self.run_shell(text + '\n').returncode, 2)

    def test_tokenizer_milestone(self):
        result = self.run_shell('tokenize "one   two\tthree"\n')
        self.assertEqual(result.stdout, '1: one\n2: two\n3: three\n')

    def test_history_across_launches_and_cd(self):
        self.assertEqual(self.run_shell('printf first\ncd /\n').returncode, 0)
        result = self.run_shell('history\nexit\n')
        self.assertIn('printf first\ncd /\nhistory\n', result.stdout)
        history = self.directory / '.shellforge_history'
        self.assertEqual(history.stat().st_mode & 0o777, 0o600)
        self.assertEqual(history.read_text(), 'printf first\ncd /\nhistory\nexit\n')

    def test_history_failure_is_nonfatal(self):
        for path in [self.directory / 'missing' / 'history', Path('relative-history')]:
            result = self.run_shell('printf still-running\n', env=dict(self.env, SHELLFORGE_HISTORY=str(path)))
            self.assertEqual(result.stdout, 'still-running')
            self.assertIn('history disabled', result.stderr)

    def test_history_refuses_symlink_and_fifo(self):
        target = self.directory / 'target'
        target.write_text('unchanged')
        link = self.directory / 'link'
        link.symlink_to(target)
        fifo = self.directory / 'fifo'
        os.mkfifo(fifo)
        for path in [link, fifo]:
            result = self.run_shell('printf ok\n', env=dict(self.env, SHELLFORGE_HISTORY=str(path)))
            self.assertEqual(result.stdout, 'ok')
            self.assertIn('history disabled', result.stderr)
        self.assertEqual(target.read_text(), 'unchanged')

    def test_background_is_nonblocking_and_shutdown_reaps(self):
        session = self.session()
        start = time.monotonic()
        session.send('sleep 20 &\nprintf "READY\\n"\n')
        pid = int(session.expect(rb'\[background (\d+)\]').group(1))
        session.expect(rb'READY\n', timeout=2)
        self.assertLess(time.monotonic() - start, 2)
        session.send('jobs\n')
        session.expect(rb'pid=' + str(pid).encode() + rb' group=')
        session.send('exit 0\n')
        self.assertEqual(session.wait(), 0)
        self.assertFalse(Path(f'/proc/{pid}').exists())

    def test_idle_background_reaping_without_more_input(self):
        session = self.session()
        session.send('sleep 0.1 &\n')
        pid = int(session.expect(rb'\[background (\d+)\]').group(1))
        deadline = time.monotonic() + 3
        while Path(f'/proc/{pid}').exists() and time.monotonic() < deadline:
            time.sleep(.02)
        self.assertFalse(Path(f'/proc/{pid}').exists(), 'background child remained alive or a zombie while shell was idle')
        session.send('exit\n')
        self.assertEqual(session.wait(), 0)

    def test_background_pipeline_stdin_and_wait_races(self):
        result = self.run_shell('cat &\nprintf "kept\\n"\n' + 'true | cat &\n' * 30 + 'false\nexit\n')
        self.assertEqual(result.stdout, 'kept\n')
        self.assertEqual(result.returncode, 1, result.stderr)

    def test_invalid_background_syntax(self):
        for text in ['&', 'sleep 1 & printf x', 'help &', 'printf x &&']:
            with self.subTest(text=text):
                self.assertEqual(self.run_shell(text + '\n').returncode, 2)

    def test_partial_pipeline_resource_failure(self):
        def limit_descriptors():
            resource.setrlimit(resource.RLIMIT_NOFILE, (6, 6))
        result = self.run_shell('printf x | cat | cat\n', preexec_fn=limit_descriptors)
        self.assertEqual(result.returncode, 1)
        self.assertIn('pipe', result.stderr)

    def test_interactive_ctrl_c_foreground_preserves_background(self):
        session = self.session(terminal=True)
        session.send('sleep 20 &\n')
        background = int(session.expect(rb'\[background (\d+)\]').group(1))
        session.expect(rb'shellforge\+ \$ ')
        session.send('sleep 20\n')
        deadline = time.monotonic() + 3
        while os.tcgetpgrp(session.fd) == session.pid and time.monotonic() < deadline:
            time.sleep(.01)
        foreground = os.tcgetpgrp(session.fd)
        self.assertNotEqual(foreground, session.pid)
        session.send(b'\x03')
        session.expect(rb'shellforge\+ \$ ')
        self.assertTrue(Path(f'/proc/{background}').exists())
        self.assertFalse(Path(f'/proc/{foreground}').exists())
        session.send('exit\n')
        self.assertEqual(session.wait(), 130)
        self.assertFalse(Path(f'/proc/{background}').exists())

    def test_interactive_idle_interrupt_and_eof(self):
        session = self.session(terminal=True)
        session.send(b'\x03')
        session.expect(rb'shellforge\+ \$ ')
        session.send('printf "alive\\n"\n')
        session.expect(rb'\r\nalive\r\n')
        session.expect(rb'shellforge\+ \$ ')
        session.send(b'\x04')
        self.assertEqual(session.wait(), 0)

    def test_foreground_terminal_input(self):
        session = self.session(terminal=True)
        session.send('cat\n')
        deadline = time.monotonic() + 3
        while os.tcgetpgrp(session.fd) == session.pid and time.monotonic() < deadline:
            time.sleep(.01)
        session.send('terminal-input\n')
        session.expect(rb'terminal-input\r\nterminal-input\r\n')
        session.send(b'\x04')
        session.expect(rb'shellforge\+ \$ ')
        session.send('exit\n')
        self.assertEqual(session.wait(), 0)


if __name__ == '__main__':
    unittest.main(verbosity=2)
