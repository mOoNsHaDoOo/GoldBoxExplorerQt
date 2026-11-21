#!/usr/bin/env python3
"""Checks `gbexplorer --ecl-basic` against golden ECL Basic listings.

Usage: check_ecl_basic.py GBEXPLORER DAX_DIR GOLDEN_DIR

GOLDEN_DIR holds eclN_ID.ecl files written by the campaign tool
`ecl_basic.py disasm ECLn.DAX GOLDEN_DIR`; DAX_DIR holds the game's ecl1.dax
and ecl2.dax (Champions of Krynn, Amiga; not part of this repository).
Every golden file must be byte-identical to the C++ output.  Exit status 0
when all are identical.
"""
import difflib
import os
import re
import subprocess
import sys


def find_dax(dax_dir, tag):
    for name in os.listdir(dax_dir):
        if name.lower() == tag.lower() + '.dax':
            return os.path.join(dax_dir, name)
    return None


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    exe, dax_dir, golden_dir = sys.argv[1:]
    goldens = sorted(f for f in os.listdir(golden_dir) if re.match(r'^ecl\d+_\d+\.ecl$', f))
    if not goldens:
        sys.exit('no eclN_ID.ecl files in %s' % golden_dir)
    same = 0
    for name in goldens:
        tag, block = re.match(r'^(ecl\d+)_(\d+)\.ecl$', name).groups()
        dax = find_dax(dax_dir, tag)
        with open(os.path.join(golden_dir, name), 'rb') as f:
            want = f.read()
        if dax is None:
            print('MISSING %s: no %s.dax in %s' % (name, tag, dax_dir))
            continue
        run = subprocess.run([exe, '--ecl-basic', dax, str(int(block))],
                             capture_output=True)
        if run.returncode == 0 and run.stdout == want:
            same += 1
            print('ok      %s' % name)
            continue
        print('DIFF    %s (exit %d) %s' % (name, run.returncode, run.stderr.decode(errors='replace').strip()))
        diff = difflib.unified_diff(want.decode('latin1').splitlines(),
                                    run.stdout.decode('latin1').splitlines(),
                                    'golden/' + name, 'gbexplorer', lineterm='', n=1)
        for i, line in enumerate(diff):
            if i >= 20:
                print('        ...')
                break
            print('        ' + line)
    print('%d/%d identical' % (same, len(goldens)))
    sys.exit(0 if same == len(goldens) else 1)


if __name__ == '__main__':
    main()
