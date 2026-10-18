"""Temporary fake game files/processes only; no installed artifacts or processes."""
import contextlib
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('native_proxy_session', HERE / 'native_proxy_session.py')
m = importlib.util.module_from_spec(spec)
with patch.object(sys, 'dont_write_bytecode', True):
    spec.loader.exec_module(m)
TEMP = Path(os.environ['LOCALAPPDATA']) / 'Temp/opencode'


def pe(*, dll=False, imported='D3DCOMPILER_43.dll'):
    data = bytearray(2048)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 60, 128)
    data[128:132] = b'PE\0\0'
    struct.pack_into('<HH', data, 132, 0x8664, 1)
    struct.pack_into('<HH', data, 148, 240, 0x2022 if dll else 0x22)
    struct.pack_into('<H', data, 152, 0x20b)
    struct.pack_into('<I', data, 152 + 108, 16)
    struct.pack_into('<II', data, 152 + 120, 0x1000, 40)
    section = 152 + 240
    struct.pack_into('<IIII', data, section + 8, 1536, 0x1000, 1536, 512)
    struct.pack_into('<I', data, 512 + 12, 0x1080)
    name = imported.encode() + b'\0'
    data[640:640 + len(name)] = name
    return bytes(data)


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value), encoding='utf-8')


@contextlib.contextmanager
def fixture(*, asi=True, log=True):
    with tempfile.TemporaryDirectory(dir=TEMP) as temporary:
        root = Path(temporary)
        repo, local = root / 'repo', root / 'local'
        binary = root / 'game/End/Binaries/Win64/ff7rebirth_.exe'
        binary.parent.mkdir(parents=True)
        binary.write_bytes(pe())
        source = root / 'artifact.dll'
        source.write_bytes(pe(dll=True))
        asi_path, log_path = binary.parent / 'FF7RPianoSongs.asi', binary.parent / 'FF7RPianoSongs.log'
        if asi:
            asi_path.write_bytes(b'original ASI')
        if log:
            log_path.write_bytes(b'old useful log')
        untouched = {}
        for filename in ('dxgi.dll', 'xinput1_3.dll', 'FF7RPianoSongs.ini', 'oo2core_9_win64.dll'):
            path = binary.parent / filename
            path.write_bytes(filename.encode())
            untouched[path] = path.read_bytes()
        game = m.fact(binary)
        save(repo / 'src/game/rva_catalog.json', {'builds': [
            {'id': 'fixture-build', 'sha256': game['sha256'], 'file_size': game['bytes']}]})
        receipt = root / 'artifact-receipt.json'
        document = {'schema': 1, 'reviewed': True, 'artifact': m.fact(source), 'build_id': 'fixture-build',
                    'game_sha256': game['sha256'], 'game_bytes': game['bytes'],
                    'source_identity': {'review': 'fixture source identity'}, 'build_identity': {'toolchain': 'fixture build'}}
        save(receipt, document)
        rows = []
        with (patch.object(m, 'ROOT', repo), patch.object(m, 'windows_processes', side_effect=AssertionError('Real process query')),
              patch.object(m.subprocess, 'run', side_effect=AssertionError('No subprocesses'))):
            options = {'process_reader': lambda: list(rows), 'local_app_data': local}
            yield root, binary, source, receipt, document, options, rows, untouched
        for path, raw in untouched.items():
            assert path.read_bytes() == raw, f'Unrelated file changed: {path}'


class SessionTests(unittest.TestCase):
    def start(self, binary, receipt, options):
        return Path(m.prepare(receipt, binary, authorize=True, **options))

    def test_prepare_collect_keep_rollback_preserves_bytes_and_evidence(self):
        with fixture() as (root, binary, source, receipt, _, options, _, _):
            session = self.start(binary, receipt, options)
            _, state, _ = m.load(session, options['local_app_data'])
            self.assertEqual(state['phase'], 'prepared')
            self.assertEqual(Path(state['target']).read_bytes(), source.read_bytes())
            self.assertFalse(Path(state['asi']).exists())
            self.assertEqual(Path(state['asi_backup']).read_bytes(), b'original ASI')
            self.assertEqual((session.parent / 'log-before.log').read_bytes(), b'old useful log')
            initial = session.read_bytes()
            with self.assertRaises(ValueError):
                m.keep(session, receipt, authorize=True, **options)
            Path(state['log']).write_text('new native proxy run; manual interpretation required', encoding='utf-8')
            result = m.collect(session, **options)
            self.assertIn('Unassessed', result['assessment'])
            report = root / 'review.txt'
            report.write_text('Reviewed focused diagnostic; retain this candidate. Not a release claim.', encoding='utf-8')
            with self.assertRaises(ValueError):
                m.keep(session, report, **options)
            self.assertEqual(m.keep(session, report, authorize=True, **options)['phase'], 'kept')
            self.assertTrue(Path(state['target']).exists())
            self.assertTrue(Path(state['asi_backup']).exists())
            self.assertEqual(m.rollback(session, **options)['phase'], 'rolled_back')
            self.assertEqual(Path(state['asi']).read_bytes(), b'original ASI')
            self.assertFalse(Path(state['target']).exists())
            self.assertTrue(Path(state['log_backup']).exists())  # Never overwrite new log.
            self.assertEqual(session.read_bytes(), initial)
            self.assertTrue((session.parent / 'log-after.log').exists())
            self.assertEqual(m.rollback(session, **options)['phase'], 'rolled_back')

    def test_no_original_asi_or_log_and_existing_proxy_refusal(self):
        with fixture(asi=False, log=False) as (_, binary, _, receipt, _, options, _, _):
            session = self.start(binary, receipt, options)
            self.assertEqual(m.rollback(session, **options)['phase'], 'rolled_back')
            self.assertFalse((binary.parent / 'FF7RPianoSongs.asi').exists())
        for name in ('D3DCOMPILER_43.dll', 'dxgi.dll', 'xinput1_3.dll'):
            with self.subTest(name=name), fixture() as (_, binary, _, receipt, document, options, _, _):
                if name == 'D3DCOMPILER_43.dll':
                    (binary.parent / name).write_bytes(b'unknown existing proxy')
                else:
                    binary.write_bytes(pe(imported=name))
                    game = m.fact(binary)
                    document.update(game_sha256=game['sha256'], game_bytes=game['bytes'])
                    save(m.ROOT / 'src/game/rva_catalog.json', {'builds': [
                        {'id': 'fixture-build', 'sha256': game['sha256'], 'file_size': game['bytes']}]})
                    save(receipt, document)
                before = {p: p.read_bytes() for p in binary.parent.iterdir()}
                with self.assertRaises(ValueError):
                    m.prepare(receipt, binary, basename=name, authorize=True, **options)
                self.assertEqual(before, {p: p.read_bytes() for p in binary.parent.iterdir()})

    def test_process_and_authorization_fail_closed(self):
        for rows in (None, {}, [{'Name': 'ff7rebirth_.exe', 'ProcessId': 1}],
                     [{'Name': 'ff7rebirth.exe', 'ProcessId': 2}], [{'Name': 'unknown'}]):
            with self.subTest(rows=rows), fixture() as (_, binary, _, receipt, _, options, _, _):
                options['process_reader'] = lambda: rows
                with self.assertRaises(ValueError):
                    self.start(binary, receipt, options)
                self.assertTrue((binary.parent / 'FF7RPianoSongs.asi').exists())
        with fixture() as (_, binary, _, receipt, _, options, _, _):
            options['process_reader'] = lambda: (_ for _ in ()).throw(OSError('CIM failed'))
            with self.assertRaises(OSError):
                self.start(binary, receipt, options)
            with self.assertRaises(ValueError):
                m.prepare(receipt, binary, **options)
        with fixture() as (_, binary, _, receipt, _, options, rows, _):
            rows.append({'Name': 'System Idle Process', 'ProcessId': 0})
            session = self.start(binary, receipt, options)
            m.rollback(session, **options)

    def test_receipt_catalog_binary_import_and_path_rejection(self):
        for case in ('source', 'receipt-hash', 'review', 'source-identity', 'build', 'game', 'missing-import', 'bad-pe', 'relative', 'basename'):
            with self.subTest(case=case), fixture() as (_, binary, source, receipt, document, options, _, _):
                if case == 'source': source.write_bytes(b'changed')
                if case == 'receipt-hash': document['artifact']['sha256'] = '0' * 64
                if case == 'review': document['reviewed'] = False
                if case == 'source-identity': document['source_identity'] = {}
                if case == 'build': document['build_id'] = 'other'
                if case == 'game': binary.write_bytes(pe() + b'changed')
                if case == 'missing-import': binary.write_bytes(pe(imported='kernel32.dll'))
                if case == 'bad-pe': binary.write_bytes(b'not a PE')
                if case == 'relative': document['artifact']['path'] = 'artifact.dll'
                save(receipt, document)
                with self.assertRaises((ValueError, OSError)):
                    m.prepare(receipt, binary, basename='anything.dll' if case == 'basename' else 'D3DCOMPILER_43.dll', authorize=True, **options)
                self.assertTrue((binary.parent / 'FF7RPianoSongs.asi').exists())
        for patcher in (lambda data: struct.pack_into('<I', data, 512 + 12, 0xffffffff),
                        lambda data: struct.pack_into('<II', data, 272, 0x1000, 65537),
                        lambda data: data.__setitem__(slice(640, 896), b'A' * 256)):
            with fixture() as (_, binary, _, _, _, _, _, _):
                data = bytearray(pe()); patcher(data); binary.write_bytes(data)
                with self.assertRaises(ValueError): m.pe_imports(binary)

    def test_failed_move_and_after_move_exception_rollback_from_intent(self):
        for after in (False, True):
            with self.subTest(after=after), fixture() as (_, binary, _, receipt, _, options, _, _):
                original = m.rename
                def fail(source, target):
                    if Path(target).name == 'D3DCOMPILER_43.dll':
                        if after: original(source, target)
                        raise OSError('injected candidate publication failure')
                    original(source, target)
                with patch.object(m, 'rename', side_effect=fail), self.assertRaisesRegex(RuntimeError, 'recovery=rolled_back'):
                    self.start(binary, receipt, options)
                self.assertEqual((binary.parent / 'FF7RPianoSongs.asi').read_bytes(), b'original ASI')
                self.assertFalse((binary.parent / 'D3DCOMPILER_43.dll').exists())
                session = next((options['local_app_data'] / 'FF7RPianoSongs/captures').glob('native-proxy-*/session.json'))
                self.assertEqual(m.load(session, options['local_app_data'])[1]['phase'], 'rolled_back')

    def test_after_move_process_open_never_restores_until_closed(self):
        with fixture() as (_, binary, _, receipt, _, options, rows, _):
            original = m.rename
            def fail(source, target):
                original(source, target)
                if Path(target).name == 'D3DCOMPILER_43.dll':
                    rows.append({'Name': 'ff7rebirth_.exe', 'ProcessId': 42})
                    raise OSError('game opened after publication')
            with patch.object(m, 'rename', side_effect=fail), self.assertRaises(RuntimeError):
                self.start(binary, receipt, options)
            self.assertFalse((binary.parent / 'FF7RPianoSongs.asi').exists())
            self.assertTrue((binary.parent / 'D3DCOMPILER_43.dll').exists())
            session = next((options['local_app_data'] / 'FF7RPianoSongs/captures').glob('native-proxy-*/session.json'))
            for action in (m.collect, m.rollback):
                with self.assertRaises(ValueError): action(session, **options)
            rows.clear()
            self.assertEqual(m.rollback(session, **options)['phase'], 'rolled_back')

    def test_second_mutation_rechecks_closure_and_changed_source(self):
        for running in (True, False):
            with self.subTest(running=running), fixture() as (_, binary, source, receipt, _, options, rows, _):
                original = m.rename
                def change(src, dst):
                    original(src, dst)
                    if Path(src).name == 'FF7RPianoSongs.asi':
                        if running: rows.append({'Name': 'ff7rebirth.exe', 'ProcessId': 42})
                        else: source.write_bytes(b'changed source after admission')
                with patch.object(m, 'rename', side_effect=change), self.assertRaises(RuntimeError):
                    self.start(binary, receipt, options)
                self.assertFalse((binary.parent / 'D3DCOMPILER_43.dll').exists())
                self.assertEqual((binary.parent / 'FF7RPianoSongs.asi').exists(), not running)

    def test_unknown_candidate_asi_conflict_backup_and_collect_failures(self):
        for case in ('candidate', 'asi', 'backup', 'unchanged-log', 'no-log', 'changed-collected'):
            with self.subTest(case=case), fixture() as (root, binary, _, receipt, _, options, _, _):
                session = self.start(binary, receipt, options)
                state = m.load(session, options['local_app_data'])[1]
                if case == 'candidate': Path(state['target']).write_bytes(b'foreign candidate')
                if case == 'asi': Path(state['asi']).write_bytes(b'foreign ASI')
                if case == 'backup': Path(state['asi_backup']).write_bytes(b'foreign backup')
                if case in {'candidate', 'asi', 'backup'}:
                    with self.assertRaises(ValueError): m.rollback(session, **options)
                    if case == 'candidate': self.assertFalse(Path(state['asi']).exists())
                    if case == 'asi': self.assertEqual(Path(state['asi']).read_bytes(), b'foreign ASI')
                    continue
                if case == 'unchanged-log': Path(state['log']).write_bytes(b'old useful log')
                if case in {'unchanged-log', 'no-log'}:
                    with self.assertRaises(ValueError): m.collect(session, **options)
                    continue
                Path(state['log']).write_bytes(b'new experiment log')
                m.collect(session, **options)
                (session.parent / 'log-after.log').write_bytes(b'changed snapshot')
                report = root / 'report.txt'; report.write_text('reviewed', encoding='utf-8')
                with self.assertRaises(ValueError): m.keep(session, report, authorize=True, **options)

    def test_checkpoint_failure_preserves_last_intent_and_recovery(self):
        with fixture() as (_, binary, _, receipt, _, options, _, _):
            original = m.publish
            failed = False
            def fail(path, value):
                nonlocal failed
                if value.get('history') and value['history'][-1]['operation'] == 'rename_asi_done': failed = True
                if failed: raise OSError('persistent disk full')
                original(path, value)
            with patch.object(m, 'publish', side_effect=fail), self.assertRaises(RuntimeError):
                self.start(binary, receipt, options)
            session = next((options['local_app_data'] / 'FF7RPianoSongs/captures').glob('native-proxy-*/session.json'))
            state = m.load(session, options['local_app_data'])[1]
            self.assertEqual(state['history'][-1]['operation'], 'rename_asi_intent')
            self.assertFalse(Path(state['asi']).exists())
            self.assertEqual(m.rollback(session, **options)['phase'], 'rolled_back')
        with fixture() as (_, binary, _, receipt, _, options, _, _):
            session = self.start(binary, receipt, options)
            original = m.publish
            def fail(path, value):
                if value.get('history') and value['history'][-1]['operation'] == 'restore_asi_done':
                    raise OSError('failed after restoring ASI')
                original(path, value)
            with patch.object(m, 'publish', side_effect=fail), self.assertRaises(OSError):
                m.rollback(session, **options)
            state = m.load(session, options['local_app_data'])[1]
            self.assertEqual(state['history'][-1]['operation'], 'restore_asi_intent')
            self.assertEqual(Path(state['asi']).read_bytes(), b'original ASI')
            self.assertFalse(Path(state['asi_backup']).exists())
            self.assertEqual(m.rollback(session, **options)['phase'], 'rolled_back')

    def test_journal_plan_path_and_existing_session_conflicts(self):
        with fixture() as (root, binary, _, receipt, _, options, _, _):
            session = self.start(binary, receipt, options)
            with self.assertRaises(ValueError): self.start(binary, receipt, options)
            with self.assertRaises(ValueError): m.rollback(root / 'unknown.json', **options)
            checkpoint = sorted(session.parent.glob('checkpoint-*.json'))[-1]
            checkpoint.write_text('{truncated', encoding='utf-8')
            with self.assertRaises(ValueError): m.rollback(session, **options)
            self.assertFalse((binary.parent / 'FF7RPianoSongs.asi').exists())

    def test_completed_rollback_cannot_remove_a_later_candidate(self):
        with fixture() as (_, binary, source, receipt, _, options, _, _):
            first = self.start(binary, receipt, options)
            m.rollback(first, **options)
            second = self.start(binary, receipt, options)
            with self.assertRaises(ValueError): m.rollback(first, **options)
            self.assertEqual((binary.parent / 'D3DCOMPILER_43.dll').read_bytes(), source.read_bytes())
            m.rollback(second, **options)

    def test_plain_paths_outside_game_and_large_catalog(self):
        for ancestor in (False, True):
            with self.subTest(ancestor=ancestor), fixture() as (_, binary, source, receipt, _, options, _, _):
                original = Path.lstat
                def reparse(path, *args, **kwargs):
                    if path == (source.parent if ancestor else source):
                        return SimpleNamespace(st_mode=0x4000 if ancestor else 0x8000, st_file_attributes=0x400)
                    return original(path, *args, **kwargs)
                with patch.object(Path, 'lstat', reparse), self.assertRaisesRegex(ValueError, 'Reparse'):
                    self.start(binary, receipt, options)
                self.assertTrue((binary.parent / 'FF7RPianoSongs.asi').exists())
        with fixture() as (_, binary, _, receipt, _, options, _, _):
            options['local_app_data'] = binary.parents[3] / 'not-local-app-data'
            with self.assertRaisesRegex(ValueError, 'outside'):
                self.start(binary, receipt, options)
            self.assertFalse(options['local_app_data'].exists())
        with fixture() as (_, binary, _, receipt, _, options, _, _):
            path = m.ROOT / 'src/game/rva_catalog.json'
            document = json.loads(path.read_bytes()); document['unrelated_catalog_data'] = 'x' * (300 * 1024)
            save(path, document)
            session = self.start(binary, receipt, options)
            m.rollback(session, **options)
        with fixture() as (_, binary, _, receipt, _, options, _, _):
            session = self.start(binary, receipt, options)
            target = binary.parent / 'D3DCOMPILER_43.dll'
            original = Path.lstat
            def reparse_target(path, *args, **kwargs):
                if path == target:
                    return SimpleNamespace(st_mode=0x8000, st_file_attributes=0x400)
                return original(path, *args, **kwargs)
            with patch.object(Path, 'lstat', reparse_target), self.assertRaisesRegex(ValueError, 'Reparse'):
                m.rollback(session, **options)
            self.assertTrue(target.exists())
            self.assertFalse((binary.parent / 'FF7RPianoSongs.asi').exists())

    def test_unpublished_plan_failure_never_moves_game_files(self):
        with fixture() as (_, binary, _, receipt, _, options, _, _):
            with patch.object(m.os, 'fsync', side_effect=OSError('disk full')), self.assertRaises(OSError):
                self.start(binary, receipt, options)
            self.assertEqual((binary.parent / 'FF7RPianoSongs.asi').read_bytes(), b'original ASI')
            self.assertFalse((binary.parent / 'D3DCOMPILER_43.dll').exists())
            self.assertFalse(list(options['local_app_data'].rglob('session.json')))

    def test_required_cli_authorizations_before_any_work(self):
        with patch.object(m, 'prepare', side_effect=AssertionError('Must parse first')), \
             patch.object(m, 'keep', side_effect=AssertionError('Must parse first')), \
             contextlib.redirect_stderr(io.StringIO()), contextlib.redirect_stdout(io.StringIO()):
            for argv in ([], ['prepare', '--artifact-receipt', 'r', '--game-binary', 'g'],
                         ['keep', '--session', 's', '--manual-report', 'r']):
                with self.assertRaises(SystemExit): m.main(argv)


if __name__ == '__main__':
    unittest.main()
