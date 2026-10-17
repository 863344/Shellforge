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
    'background': 'sleep 0.01 &\ntrue | cat &\nsleep 0.05\njobs\nsleep 20 &\nexit 0\n',
    'disabled-history': 'history\nprintf ok\nexit 0\n',
}
with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    for name, script in cases.items():
        case = root / name
        case.mkdir()
        history = case / '.shellforge_history' if name != 'disabled-history' else case / 'missing' / 'history'
        env = dict(os.environ, HOME=str(case), SHELLFORGE_HISTORY=str(history), VG_BIG='v' * 65537)
        command = ['valgrind', '--leak-check=full', '--show-leak-kinds=all',
            '--errors-for-leak-kinds=definite,indirect,possible', '--error-exitcode=99',
            '--track-fds=yes', '--log-file=' + str(case / 'valgrind-%p.log'), shell]
        result = subprocess.run(command, input=script, text=True, capture_output=True,
            env=env, cwd=case, timeout=30)
        logs = list(case.glob('valgrind-*.log'))
        if result.returncode or not logs:
            print(result.stdout, result.stderr)
            for log in logs:
                print(log.read_text())
            raise SystemExit(f'FAIL Valgrind {name}: status {result.returncode}')
        for log in logs:
            text = log.read_text()
            errors = re.search(r'ERROR SUMMARY: ([\d,]+) errors', text)
            if not errors or int(errors[1].replace(',', '')):
                print(text)
                raise SystemExit(f'FAIL Valgrind errors in {name}')
            if re.search(r'(definitely|indirectly|possibly) lost: [1-9]', text):
                print(text)
                raise SystemExit(f'FAIL Valgrind lost memory in {name}')
        print(f'PASS Valgrind {name}: {len(logs)} process report(s), zero errors/lost blocks')
print('All six Valgrind scenarios passed. Fork children can report inherited reachable memory before failed exec; it is not lost memory.')
