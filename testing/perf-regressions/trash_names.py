"""Exercise the Finder branch with an osascript stub and no filesystem deletion."""
from common import ROOT
import os
from pathlib import Path
import subprocess
import tempfile

source = (ROOT / 'far2l/bootstrap/trash.sh').read_text()
start = source.index('\n\tosascript')
end = source.index('\n\nelse', start)
branch = source[start:end]
with tempfile.TemporaryDirectory(prefix='far2l-trash-') as directory:
    path = Path(directory)
    stub = path / 'osascript'
    stub.write_text(
        '#!/bin/sh\n'
        '[ "$#" -eq 2 ] && [ "$1" = - ] || exit 3\n'
        'printf "%s" "$2"\n'
        '/bin/cat > "$FAR2L_TEST_TRASH_SCRIPT"\n'
    )
    stub.chmod(0o700)
    script_log = path / 'script'
    env = dict(os.environ, PATH=directory, FAR2L_TEST_TRASH_SCRIPT=str(script_log))
    scripts = []
    for name in ["O'Brien.txt", 'a "quoted" name.txt', 'a\\b.txt', 'a\nb.txt']:
        original = str(path / name)
        result = subprocess.run(
            ['/bin/sh', '-ec', branch, 'trash.sh', original, str(path / 'error')],
            check=True, capture_output=True, env=env,
        )
        assert result.stdout == os.fsencode(original)
        scripts.append(script_log.read_text())
    assert len(set(scripts)) == 1
    assert 'item 1 of argv' in scripts[0]
print('Finder trash preserves quoted and literal path characters: PASS')
