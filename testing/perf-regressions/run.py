"""Run the focused performance-report regressions with ASan and UBSan."""
from pathlib import Path
import subprocess
import sys

directory = Path(__file__).resolve().parent
tests = sorted(path for path in directory.glob('*.py')
               if path.name not in {'common.py', 'run.py'}
               and not path.name.endswith('_fixture.py'))
for path in tests:
    print(f'Running {path.name}', flush=True)
    subprocess.run([sys.executable, '-B', str(path)], check=True)
print(f'All {len(tests)} sanitizer regression checks passed.', flush=True)
