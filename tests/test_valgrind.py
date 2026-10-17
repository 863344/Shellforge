"""Real Valgrind runs for success, parser, exec, file, env, and history paths."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

shell = str(Path(__file__).resolve().parents[1] / 'shellforge-plus')
if not shutil.which('valgrind'):
    raise SystemExit('Valgrind is required: sudo apt install valgrind')
cases = {
    'success': 'printf "b\\na\\n" | sort\nprintf x > file\ncat < file\ntokenize "a b"\nhistory\nexit 0\n',
    'parse-errors': 'printf "unfinished\nprintf ${BAD\ncat > a > b\n&\n' + 'x ' * 257 + '\nexit 0\n',
    'process-and-file-errors': 'missing_shellforge_command\ncat < absent\nprintf x > absent-dir/file\ncd absent-dir\nexit 0\n',
    'expansion': 'printf "%s" "$HOME"\nprintf "%s" \'$HOME\'\nprintf "%s" "${VG_BIG}"\nexit 0\n',
    'background': 'sleep 0.01 &\ntrue | cat &\nsleep 0.5\njobs\nsleep 20 &\nsleep 0.5\nexit 0\n',
    'disabled-history': 'history\nprintf ok\nexit 0\n',
}
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    for name, script in cases.items():
        case = root / name
        case.mkdir()
        history = case / '.shellforge_history' if name != 'disabled-history' else case / 'missing' / 'history'
        env = dict(os.environ, HOME=str(case), SHELLFORGE_HISTORY=str(history), VG_BIG='v' * 65537)
        # Audit our program, not allocations made by installed external utilities.
        command = ['valgrind', '--trace-children=no', '--leak-check=full', '--show-leak-kinds=all',
            '--errors-for-leak-kinds=definite,indirect,possible', '--error-exitcode=99',
            '--track-fds=yes', '--log-file=' + str(case / 'valgrind-%p.log'), shell]
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, text=True, env=env, cwd=case)
        try:
            stdout, stderr = process.communicate(script, timeout=30)
        except subprocess.TimeoutExpired:
            process.kill()
            process.communicate()
            raise
        logs = list(case.glob('valgrind-*.log'))
        if process.returncode or not logs:
            print(stdout, stderr)
            for log in logs:
                print(log.read_text())
            raise SystemExit(f'FAIL Valgrind {name}: status {process.returncode}')
        reports = 0
        handoffs = 0
        for log in logs:
            text = log.read_text()
            errors = re.search(r'ERROR SUMMARY: ([\d,]+) errors', text)
            if not errors:
                # A successful exec ends instrumentation with trace-children=no.
                # Require a complete parent report and reject child diagnostics.
                lines = [re.sub(r'^==\d+== ?', '', line).strip() for line in text.splitlines()]
                prefixes = ('Memcheck,', 'Copyright ', 'Using Valgrind-', 'Command:', 'Parent PID:')
                if log.name == f'valgrind-{process.pid}.log' or any(
                    line and not line.startswith(prefixes) for line in lines):
                    print(text)
                    raise SystemExit(f'FAIL incomplete or diagnostic Valgrind report in {name}')
                handoffs += 1
                continue
            reports += 1
            if int(errors[1].replace(',', '')):
                print(text)
                raise SystemExit(f'FAIL Valgrind errors in {name}')
            if re.search(r'(definitely|indirectly|possibly) lost: [1-9]', text):
                print(text)
                raise SystemExit(f'FAIL Valgrind lost memory in {name}')
        if not reports:
            raise SystemExit(f'FAIL no completed Shellforge memory audit in {name}')
        print(f'PASS Valgrind {name}: {reports} completed Shellforge report(s), {handoffs} exec handoff(s), zero errors/lost blocks')
print('All six Shellforge Valgrind scenarios passed. External utilities are outside the audit. Failed-exec children may retain inherited reachable memory, not lost memory.')
