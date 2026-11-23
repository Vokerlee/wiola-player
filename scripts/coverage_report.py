#!/usr/bin/env python3
"""Reproducible LLVM coverage for explicitly inventoried first-party targets."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

WORKFLOW = {'gui', 'desktop', 'app', 'persistence', 'library', 'media'}
METRICS = ('lines', 'functions', 'regions', 'branches')


def run(command, **kwargs):
    return subprocess.run([str(item) for item in command], check=True, **kwargs)


def capture(command):
    return subprocess.check_output([str(item) for item in command], text=True).strip()


def source_state(root):
    changed = capture(['git', 'diff', 'HEAD', '--name-only', '--', 'src', 'tests', 'cmake',
                       'scripts', '.github', 'CMakeLists.txt', 'CMakePresets.json'])
    if changed:
        raise ValueError('Coverage requires a committed tested revision:\n' + changed)
    files = capture(['git', 'ls-files', 'src', 'tests', 'cmake', 'scripts',
                     'CMakeLists.txt', 'CMakePresets.json']).splitlines()
    digest = hashlib.sha256()
    for name in sorted(files):
        digest.update(name.encode() + b'\0' + (root / name).read_bytes())
    return {'revision': capture(['git', 'rev-parse', 'HEAD']), 'source_sha256': digest.hexdigest()}


def inventory(root, build):
    reply = build / '.cmake/api/v1/reply'
    indexes = sorted(reply.glob('index-*.json'), key=lambda p: p.stat().st_mtime_ns)
    if not indexes:
        raise ValueError('CMake file API returned no codemodel')
    index = json.loads(indexes[-1].read_text())
    model = json.loads((reply / index['reply']['codemodel-v2']['jsonFile']).read_text())
    production, tests, sources = {}, {}, set()
    for item in model['configurations'][0]['targets']:
        target = json.loads((reply / item['jsonFile']).read_text())
        name, kind = target['name'], target['type']
        target_sources = [Path(s['path']) for s in target.get('sources', [])
                          if not s.get('isGenerated')]
        source_names = [p.as_posix() if not p.is_absolute() else p.relative_to(root).as_posix()
                        for p in target_sources if not p.is_absolute() or p.is_relative_to(root)]
        artifacts = [(build / p['path']).resolve() for p in target.get('artifacts', [])]
        if ((kind == 'STATIC_LIBRARY' and name.startswith('wiola_')) or name == 'wiola-player'):
            owned = [s for s in source_names if s.startswith('src/')]
            if owned:
                production[name] = {'artifacts': [str(p) for p in artifacts], 'sources': owned}
                sources.update(owned)
        elif kind == 'EXECUTABLE' and name.endswith('_test') and artifacts:
            tests[name] = {'binary': str(artifacts[0]), 'sources': source_names,
                           'workflow': any(s.split('/')[1] in WORKFLOW for s in source_names
                                           if s.startswith('tests/') and len(s.split('/')) > 2)}
    if 'wiola-player' not in production or not tests or not sources:
        raise ValueError('Incomplete first-party production/test inventory')
    return {'production': production, 'tests': tests, 'sources': sorted(sources)}


def require_profiles(raw, expected):
    profiles = sorted(raw.glob('*.profraw'))
    if len(profiles) < expected or any(p.stat().st_size == 0 for p in profiles):
        raise ValueError(f'{raw.name}: expected at least {expected} complete process profiles, got {len(profiles)}')
    return profiles


def workflow_file(filename, root):
    path = Path(filename).resolve()
    return path.is_relative_to(root / 'src') and path.relative_to(root / 'src').parts[0] in WORKFLOW


def summarize(files):
    result = {}
    for metric in METRICS:
        count = sum(f['summary'][metric]['count'] for f in files)
        covered = sum(f['summary'][metric]['covered'] for f in files)
        result[metric] = {'count': count, 'covered': covered, 'uncovered': count - covered}
    return result


def coverage_diagnostics(stderr):
    # LLVM emits zero-hash entries for uninstantiated mappings, including an inline function
    # mapped in several objects but instantiated in only some. Keep these visible; reject real
    # profile/CFG hash mismatches rather than silently accepting any warning.
    clean = re.sub(r"\x1b\[[0-9;]*m", "", stderr)
    records = []
    expected = 0
    for line in clean.splitlines():
        if not line.strip(): continue
        count = re.fullmatch(r"warning: (\d+) functions have mismatched data", line)
        if count:
            expected += int(count.group(1)); continue
        entry = re.fullmatch(r"hash-mismatch: No profile record found for '(.+)' with hash = (0x[0-9a-fA-F]+)", line)
        if not entry or int(entry.group(2), 16) != 0:
            raise ValueError('Invalid coverage profile diagnostic: ' + line)
        records.append(entry.group(1))
    if expected != len(records):
        raise ValueError('Coverage diagnostic count did not match its detailed records')
    return records


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scope', choices=['all', 'workflow'], default='all')
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--build-dir', type=Path, default=Path('build-coverage'))
    args = parser.parse_args()
    root = Path(capture(['git', 'rev-parse', '--show-toplevel'])).resolve()
    os.chdir(root)
    before = source_state(root)
    build = args.build_dir.resolve()
    query = build / '.cmake/api/v1/query'
    query.mkdir(parents=True, exist_ok=True)
    (query / 'codemodel-v2').touch()
    run(['cmake', '--preset', 'linux-coverage', '-B', build])
    entries = inventory(root, build)
    selected = {name: value for name, value in entries['tests'].items()
                if args.scope == 'all' or value['workflow']}
    targets = ['wiola_coverage_inventory', *entries['production'], *selected]
    run(['cmake', '--build', build, '-j', args.jobs, '--target', *targets])
    # Verify the actual configured compiler, not merely a shell default named clang.
    cache = (build / 'CMakeCache.txt').read_text()
    compiler = re.search(r'^CMAKE_CXX_COMPILER:[^=]+=(.+)$', cache, re.M).group(1)
    compiler_version = capture([compiler, '--version'])
    match = re.search(r'clang version (\d+)', compiler_version)
    if not match:
        raise ValueError('Configured compiler is not Clang')
    major = match.group(1)
    profdata = shutil.which('llvm-profdata-' + major)
    cov = shutil.which('llvm-cov-' + major)
    if not profdata or not cov:
        raise ValueError('Matching LLVM profile tools are missing for compiler ' + major)
    tool_versions = {tool: capture([path, '--version']) for tool, path in
                     [('compiler', compiler), ('profdata', profdata), ('cov', cov)]}
    for tool in ['profdata', 'cov']:
        if not re.search(r'version\s+' + major + r'\.', tool_versions[tool], re.I):
            raise ValueError('Mismatched ' + tool + ' version')
    report = build / 'coverage'
    if report.exists():
        shutil.rmtree(report)
    report.mkdir()
    (report / 'inventory.json').write_text(json.dumps(entries, indent=2) + '\n')
    discovery = json.loads(capture(['ctest', '--test-dir', build, '--show-only=json-v1']))
    groups = {name: [] for name in selected}
    for test in discovery['tests']:
        binaries = {Path(str(a)).name.removesuffix('.exe') for a in test.get('command', [])}
        for name in binaries & selected.keys():
            groups[name].append(test['name'])
    if any(not cases for cases in groups.values()):
        raise ValueError('Selected test executable missing from CTest discovery')
    profiles, completed = [], {}
    for target, cases in sorted(groups.items()):
        raw = report / 'raw' / target
        raw.mkdir(parents=True)
        environment = dict(os.environ, LLVM_PROFILE_FILE=str(raw / '%m-%p.profraw'))
        expression = '^(' + '|'.join(re.escape(name) for name in cases) + ')$'
        with (report / (target + '.log')).open('w') as output:
            run(['ctest', '--test-dir', build, '-R', expression, '--no-tests=error',
                 '--timeout', '120', '--output-on-failure'], env=environment,
                stdout=output, stderr=subprocess.STDOUT)
        produced = require_profiles(raw, len(cases))
        profiles.extend(produced)
        completed[target] = {'tests': cases, 'profiles': len(produced)}
        print(f'{target}: {len(cases)} tests, {len(produced)} profiles', flush=True)
    merged = report / 'wiola.profdata'
    run([profdata, 'merge', '-sparse', *profiles, '-o', merged])
    objects = [build / 'bin/wiola-player', build / 'bin/wiola_coverage_inventory']
    objects.extend(Path(value['binary']) for value in selected.values())
    if any(not p.is_file() for p in objects):
        raise ValueError('An inventoried production/test object is absent')
    arguments = [objects[0], *[part for p in objects[1:] for part in ['-object', p]],
                 '-instr-profile=' + str(merged), str(root / 'src')]
    diagnostics = {}
    def measured(command, destination=None):
        result = run([cov, *command, *(['-dump'] if command[0] == 'report' else [])],
                     stdout=destination, stderr=subprocess.PIPE, text=True)
        if command[0] == 'report':
            diagnostics['report'] = {'zero_hash_mappings': coverage_diagnostics(result.stderr)}
        else:
            clean = re.sub(r'\x1b\[[0-9;]*m', '', result.stderr)
            counts = []
            for line in clean.splitlines():
                if not line.strip(): continue
                warning = re.fullmatch(r'warning: (\d+) functions have mismatched data', line)
                if not warning: raise ValueError('Unexpected coverage diagnostic: ' + line)
                counts.append(int(warning.group(1)))
            if sum(counts) != len(diagnostics['report']['zero_hash_mappings']):
                raise ValueError('Coverage commands disagree about mapping diagnostics')
        (report / (command[0] + '-mapping-diagnostics.txt')).write_text(result.stderr)

    for command, output in [('report', 'summary.txt'), ('export', 'summary.json')]:
        options = ['-format=text', '-summary-only'] if command == 'export' else []
        with (report / output).open('w') as destination:
            measured([command, *arguments, *options], destination)
    with (report / 'coverage.lcov').open('w') as destination:
        measured(['export', *arguments, '-format=lcov'], destination)
    measured(['show', *arguments, '-format=html', '-output-dir=' + str(report / 'html')])
    data = json.loads((report / 'summary.json').read_text())['data'][0]
    mapped = {Path(f['filename']).resolve() for f in data['files']}
    absent = [s for s in entries['sources'] if (root / s).resolve() not in mapped]
    if absent:
        raise ValueError('Production sources absent from coverage mapping: ' + ', '.join(absent))
    files = [f for f in data['files'] if workflow_file(f['filename'], root)]
    workflow = {'totals': summarize(files), 'files': [
        {'file': str(Path(f['filename']).relative_to(root)), 'counts': f['summary']}
        for f in files]}
    (report / 'workflow.json').write_text(json.dumps(workflow, indent=2) + '\n')
    (report / 'workflow.txt').write_text('\n'.join(
        f'{name}: {v["covered"]}/{v["count"]}; uncovered={v["uncovered"]}'
        for name, v in workflow['totals'].items()) + '\n')
    after = source_state(root)
    if before != after:
        raise ValueError('Tested source changed during measurement')
    manifest = dict(before, scope=args.scope, platform=os.name, tools=tool_versions,
                    completed=completed, raw_profiles=len(profiles), mapping_diagnostics=diagnostics, exclusions=[
                        'external dependencies and generated files: only src/ is selected',
                        'compile/link boundary executables are not production targets'])
    (report / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    percent = data['totals']['lines']['percent']
    (report / 'html/badge.json').write_text(json.dumps({
        'schemaVersion': 1, 'label': 'coverage', 'message': f'{percent:.1f}%',
        'color': 'brightgreen' if percent >= 90 else 'yellow' if percent >= 75 else 'red'}) + '\n')
    print((report / 'workflow.txt').read_text())
    print('Reports: ' + str(report))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
