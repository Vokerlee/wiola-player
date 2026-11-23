#!/usr/bin/env python3
"""Run the same isolated desktop contracts against a packaged native executable."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--artifacts', type=Path, required=True)
    parser.add_argument('--revision', required=True)
    parser.add_argument('--filter', default='DesktopJourney.*')
    parser.add_argument('--minimum-tests', type=int, default=2)
    parser.add_argument('--media-dir', type=Path)
    args = parser.parse_args()
    binary = args.binary.resolve()
    artifacts = args.artifacts.resolve()
    artifacts.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ, WIOLA_GUI_FAILURE_ARTIFACTS=str(artifacts))
    environment.setdefault('QT_QPA_PLATFORM', 'windows' if os.name == 'nt' else 'offscreen')
    if args.media_dir:
        environment['WIOLA_JOURNEY_MEDIA_DIR'] = str(args.media_dir.resolve())
    manifest = {'revision': args.revision, 'binary': str(binary),
                'sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
                'platform': platform.platform(), 'qt_platform': environment['QT_QPA_PLATFORM']}
    (artifacts / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    (artifacts / 'results.xml').unlink(missing_ok=True)
    command = [str(binary), '--gtest_filter=' + args.filter,
               '--gtest_output=xml:' + str(artifacts / 'results.xml')]
    with (artifacts / 'journey.log').open('w', encoding='utf-8') as log:
        try:
            result = subprocess.run(command, env=environment, stdout=log,
                                    stderr=subprocess.STDOUT, timeout=120)
        except subprocess.TimeoutExpired:
            print('Desktop journey timed out; see ' + str(artifacts))
            return 1
    print((artifacts / 'journey.log').read_text(encoding='utf-8', errors='replace'))
    if not (artifacts / 'results.xml').exists():
        raise SystemExit('Desktop journeys produced no test result')
    import xml.etree.ElementTree as ET
    results = ET.parse(artifacts / 'results.xml').getroot()
    if int(results.attrib.get('tests', 0)) < args.minimum_tests:
        raise SystemExit('Desktop journey filter selected fewer than the required contracts')
    return result.returncode


if __name__ == '__main__':
    raise SystemExit(main())
