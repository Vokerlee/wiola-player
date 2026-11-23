#!/usr/bin/env python3
"""Seed bounded GUI regressions, require assertion failures, and restore the exact source."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import signal
import subprocess
import xml.etree.ElementTree as ET

SEEDS = {
    'wrong-document': (
        'src/gui/shell/document_command_router.cpp',
        'std::optional<library::PlaylistId> destination = library_->active_playlist().id();',
        'std::optional<library::PlaylistId> destination = context.document.playlist() ? std::optional{*context.document.playlist()} : std::nullopt;',
        'gui_document_command_router_test', 'DocumentCommandRouterTest.ContextMenuTransferAfterCloseAndReopenUsesOneExactOccurrence'),
    'stale-revision': (
        'src/desktop/library/library_session.cpp',
        'if (revision != pending->second) {', 'if (false) {',
        'desktop_library_lifetime_test', 'LibraryLifetime.CapturedReadAfterPhysicalWriteCannotOverwriteNewEmbeddedMetadata'),
    'rollback': (
        'src/library/repository_transaction.cpp',
        '    restore(repository_.data_.tracks_, tracks_);', '    // Seeded regression: omit restoring touched records.',
        'library_repository_transaction_test', 'RepositoryTransaction.PersistenceFailureRestoresValuesRevisionsAndEveryAllocator'),
    'selection': (
        'src/gui/tracks/track_table_view.cpp',
        'for (const auto& id : selected) {', 'for (const auto& id : std::vector<TrackRowId>{}) {',
        'gui_track_table_view_test', 'TrackTableView.AddsTagColumnsWithoutLosingSelectionAndUpdatesTheirValues'),
    'action-binding': (
        'src/gui/shell/action_button_binding.cpp',
        'button.setEnabled(action.isEnabled());', 'button.setEnabled(true);',
        'gui_layout_component_contract_test', 'AllContainers/LayoutComponentContract.*'),
    'preview-side-effects': (
        'src/gui/layout/layout_builder.cpp',
        'node.view && !context.isolated_preview ? views_.find(*node.view) : nullptr;',
        'node.view ? views_.find(*node.view) : nullptr;',
        'gui_layout_component_contract_test', 'AllContainers/LayoutComponentContract.*'),
}


def assertion_failure(path, returncode):
    if not path.is_file() or returncode <= 0:
        return False
    result = ET.parse(path).getroot()
    return (int(result.attrib.get('tests', 0)) > 0
            and int(result.attrib.get('failures', 0)) > 0
            and int(result.attrib.get('errors', 0)) == 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, required=True)
    parser.add_argument('--artifacts', type=Path, required=True)
    parser.add_argument('--seed', choices=list(SEEDS), nargs='+', default=list(SEEDS))
    parser.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args()
    root = Path(subprocess.check_output(['git', 'rev-parse', '--show-toplevel'], text=True).strip())
    os.chdir(root)
    subprocess.run(['git', 'diff', '--exit-code', 'HEAD', '--', 'src', 'tests', 'scripts', 'cmake'], check=True, stdout=subprocess.DEVNULL)
    artifacts = args.artifacts.resolve()
    artifacts.mkdir(parents=True, exist_ok=True)
    build = args.build_dir.resolve()
    reports = []

    def build_target(target, label):
        with (artifacts / (label + '-build.log')).open('w') as log:
            subprocess.run(['cmake', '--build', str(build), '-j', str(args.jobs), '--target', target],
                           check=True, stdout=log, stderr=subprocess.STDOUT, timeout=900)

    def execute(target, selection, label):
        candidates = list(build.glob('tests/**/' + target + ('.exe' if os.name == 'nt' else '')))
        if len(candidates) != 1:
            raise RuntimeError('Ambiguous/missing mutation executable: ' + target)
        xml = artifacts / (label + '.xml')
        xml.unlink(missing_ok=True)
        with (artifacts / (label + '.log')).open('w') as log:
            result = subprocess.run([str(candidates[0]), '--gtest_filter=' + selection,
                                     '--gtest_output=xml:' + str(xml)],
                                    stdout=log, stderr=subprocess.STDOUT, timeout=60)
        return result.returncode, xml

    def interrupted(signum, frame):
        raise KeyboardInterrupt('Interrupted mutation check')
    signal.signal(signal.SIGTERM, interrupted)
    for name in args.seed:
        source, old, new, target, selection = SEEDS[name]
        path = root / source
        original = path.read_bytes()
        text = original.decode('utf-8')
        if text.count(old) != 1:
            raise RuntimeError('Mutation anchor drifted: ' + name)
        build_target(target, name + '-original')
        status, xml = execute(target, selection, name + '-original')
        if status != 0 or not xml.is_file() or int(ET.parse(xml).getroot().attrib.get('tests', 0)) == 0:
            raise RuntimeError('The original contract did not pass: ' + name)
        mutant = text.replace(old, new).encode('utf-8')
        row = {'seed': name, 'source': source, 'target': target, 'filter': selection,
               'original_sha256': hashlib.sha256(original).hexdigest(),
               'mutant_sha256': hashlib.sha256(mutant).hexdigest()}
        try:
            path.write_bytes(mutant)
            build_target(target, name + '-mutant')
            status, xml = execute(target, selection, name + '-mutant')
            row['killed_by_assertion'] = assertion_failure(xml, status)
            if not row['killed_by_assertion']:
                raise RuntimeError('Regression survived, crashed, or produced no assertion evidence: ' + name)
        finally:
            if path.read_bytes() != mutant:
                raise RuntimeError('Source changed during mutation; refusing to overwrite: ' + source)
            path.write_bytes(original)
            build_target(target, name + '-restored')
        status, xml = execute(target, selection, name + '-restored')
        if status != 0:
            raise RuntimeError('Restored contract failed: ' + name)
        row['restored_sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
        reports.append(row)
        (artifacts / 'mutations.json').write_text(json.dumps(reports, indent=2) + '\n')
        print(name + ': seeded assertion detected; original restored and passed', flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
