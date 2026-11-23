#!/usr/bin/env python3
"""Build and run an explicit set of affected test executables; never silently run the suite."""
import argparse
import json
from pathlib import Path
import re
import subprocess


def select_tests(discovered, targets):
    selected, matched = [], set()
    for test in discovered['tests']:
        found = {Path(str(arg).replace('\\', '/')).name.removesuffix('.exe')
                 for arg in test.get('command', [])} & set(targets)
        if found:
            matched.update(found)
            selected.append(test['name'])
    missing = set(targets) - matched
    if missing:
        raise ValueError('No discovered test command for: ' + ', '.join(sorted(missing)))
    if not selected or len(selected) != len(set(selected)):
        raise ValueError('Empty or ambiguous affected test selection')
    return selected


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', required=True, type=Path)
    parser.add_argument('--target', required=True, nargs='+', help='Affected test target names')
    parser.add_argument('--repeat', type=int, default=1)
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--no-build', action='store_true')
    args = parser.parse_args()
    if args.repeat < 1 or args.jobs < 1:
        parser.error('repeat and jobs must be positive')
    if not args.no_build:
        subprocess.run(['cmake', '--build', str(args.build_dir), '-j', str(args.jobs),
                        '--target', *args.target], check=True)
    discovered = json.loads(subprocess.check_output(
        ['ctest', '--test-dir', str(args.build_dir), '--show-only=json-v1'], text=True))
    selected = select_tests(discovered, args.target)
    output = args.build_dir / 'Testing' / 'AffectedTests'
    output.mkdir(parents=True, exist_ok=True)
    (output / 'selected.json').write_text(json.dumps(selected, indent=2) + '\n')
    print(f'Selected {len(selected)} cases from {len(args.target)} affected executables.', flush=True)
    command = ['ctest', '--test-dir', str(args.build_dir), '-R',
               '^(' + '|'.join(re.escape(name) for name in selected) + ')$',
               '--output-on-failure', '--no-tests=error', '--timeout', '120']
    if args.repeat > 1:
        command += ['--repeat', f'until-fail:{args.repeat}']
    return subprocess.call(command)


if __name__ == '__main__':
    raise SystemExit(main())
