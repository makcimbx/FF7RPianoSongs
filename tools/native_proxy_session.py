"""One reviewed normal-import DLL experiment; never launches or controls a process.

prepare(receipt_path, game_binary, *, basename='D3DCOMPILER_43.dll', authorize=False)
collect(session_path), rollback(session_path), keep(session_path, manual_report, *, authorize=False)
All accept process_reader/local_app_data for isolated tests. Receipt schema is
documented in BuildAndRelease.md. Windows rename publication is non-overwriting.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import struct
import subprocess
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
PROFILES = {'d3dcompiler_43.dll', 'd3d9.dll', 'd3d11.dll', 'dsound.dll',
            'winmm.dll', 'dxgi.dll', 'xinput1_3.dll'}
PRODUCT = 'FF7RPianoSongs'
JSON_LIMIT = 256 * 1024
FILE_LIMIT = 512 * 1024 * 1024


def require(value, message):
    if not value:
        raise ValueError(message)


def plain(value, *, exists=True):
    path = Path(value)
    require(path.is_absolute() and re.fullmatch('[A-Za-z]:', path.drive) and '..' not in path.parts and
            ':' not in str(path)[2:] and all(part == part.rstrip(' .') for part in path.parts[1:]), 'Expected absolute plain local path')
    for item in (path, *path.parents):
        try:
            info = item.lstat()
        except FileNotFoundError:
            continue
        require(not stat.S_ISLNK(info.st_mode) and not getattr(info, 'st_file_attributes', 0) & 0x400,
                'Reparse/symlink paths are unsupported')
    if exists:
        require(path.is_file(), f'Expected regular file: {path}')
    return path


def fact(path, limit=FILE_LIMIT):
    path = plain(path)
    before = path.stat()
    require(0 <= before.st_size <= limit, 'File exceeds bound')
    digest = hashlib.sha256()
    total = 0
    with path.open('rb') as stream:
        while block := stream.read(1024 * 1024):
            total += len(block)
            require(total <= limit, 'File grew beyond bound')
            digest.update(block)
    after = path.stat()
    require((before.st_size, before.st_mtime_ns, before.st_ino) ==
            (after.st_size, after.st_mtime_ns, after.st_ino) and total == before.st_size, 'File changed while hashing')
    return {'path': str(path), 'bytes': total, 'sha256': digest.hexdigest()}


def expect(path, expected):
    actual = fact(path)
    require(actual['bytes'] == expected['bytes'] and actual['sha256'] == expected['sha256'], f'Changed bytes: {path}')
    return actual


def read_json(path, limit=JSON_LIMIT):
    path = plain(path)
    with path.open('rb') as stream:
        raw = stream.read(limit + 1)
    require(len(raw) <= limit, 'JSON exceeds bound')
    value = json.loads(raw)
    require(isinstance(value, dict), 'Expected JSON object')
    return value


def rename(source, target):
    require(os.name == 'nt', 'Filesystem transactions require Windows non-overwriting rename')
    plain(source)
    target = plain(target, exists=False)
    require(not target.exists(), f'Destination exists: {target}')
    os.rename(source, target)  # Windows does not replace an existing destination.


def publish(path, value):
    raw = (json.dumps(value, indent=2) + '\n').encode('utf-8')
    require(len(raw) <= JSON_LIMIT, 'Journal exceeds bound')
    path = plain(path, exists=False)
    temporary = path.with_name(path.name + '.' + uuid.uuid4().hex + '.tmp')
    with temporary.open('xb') as stream:
        require(stream.write(raw) == len(raw), 'Short journal write')
        stream.flush()
        os.fsync(stream.fileno())
    require(temporary.read_bytes() == raw, 'Incomplete journal write')
    rename(temporary, path)  # Preserve failed temps; never publish truncated final JSON.


def copy_new(source, target, expected):
    source = plain(source)
    target = plain(target, exists=False)
    with source.open('rb') as incoming, target.open('xb') as outgoing:
        remaining = expected['bytes']
        while remaining:
            block = incoming.read(min(1024 * 1024, remaining))
            require(block, 'Truncated copy source')
            require(outgoing.write(block) == len(block), 'Short copy')
            remaining -= len(block)
        require(not incoming.read(1), 'Copy source grew')
        outgoing.flush()
        os.fsync(outgoing.fileno())
    expect(target, expected)


def windows_processes():
    shell = Path(os.environ['SystemRoot']) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
    result = subprocess.run([str(plain(shell)), '-NoProfile', '-NonInteractive', '-Command',
        '[Console]::OutputEncoding=[Text.UTF8Encoding]::new(); '
        'ConvertTo-Json -Compress -InputObject @(Get-CimInstance Win32_Process -ErrorAction Stop | Select-Object Name,ProcessId)'],
        capture_output=True, encoding='utf-8-sig', timeout=20, check=True)
    return json.loads(result.stdout)


def closed(reader):
    rows = (reader or windows_processes)()
    require(isinstance(rows, list) and all(isinstance(row, dict) and isinstance(row.get('Name'), str)
            and type(row.get('ProcessId')) is int and row['ProcessId'] >= 0 for row in rows), 'Unknown process state')
    require(not any(row['Name'].lower() in {'ff7rebirth_.exe', 'ff7rebirth.exe'} for row in rows),
            'Game or bootstrap is running; close it normally first')


def pe_imports(path, *, dll=False):
    """Bounded disk-only PE32+ headers/import descriptors; no mapped image or execution."""
    path = plain(path)
    size = path.stat().st_size
    require(size <= FILE_LIMIT, 'PE exceeds bound')
    with path.open('rb') as stream:
        def read(offset, count):
            require(0 <= offset <= size and 0 < count <= 65536 and offset + count <= size, 'PE range rejected')
            stream.seek(offset)
            data = stream.read(count)
            require(len(data) == count, 'Short PE read')
            return data
        dos = read(0, 64)
        require(dos[:2] == b'MZ', 'DOS signature')
        offset = struct.unpack_from('<I', dos, 60)[0]
        require(64 <= offset <= 1024 * 1024, 'PE header offset')
        header = read(offset, 24)
        machine, sections = struct.unpack_from('<HH', header, 4)
        optional_size, flags = struct.unpack_from('<HH', header, 20)
        require(header[:4] == b'PE\0\0' and machine == 0x8664 and 1 <= sections <= 96 and
                128 <= optional_size <= 4096 and bool(flags & 0x2000) == dll, 'Expected x64 executable/DLL')
        optional = read(offset + 24, optional_size)
        require(struct.unpack_from('<H', optional)[0] == 0x20b, 'Expected PE32+')
        if dll:
            return set()
        require(struct.unpack_from('<I', optional, 108)[0] >= 2, 'No import directory')
        rva, length = struct.unpack_from('<II', optional, 120)
        require(rva and 20 <= length <= 65536, 'Invalid import directory')
        table = read(offset + 24 + optional_size, sections * 40)
        def at(address, count):
            for index in range(sections):
                virtual, raw_size, raw = struct.unpack_from('<III', table, index * 40 + 12)
                if address >= virtual and address - virtual + count <= raw_size:
                    return read(raw + address - virtual, count)
            raise ValueError('Import outside raw section')
        imports = set()
        for index in range(min(length // 20, 128)):
            item = at(rva + index * 20, 20)
            if item == bytes(20):
                return imports
            name = struct.unpack_from('<I', item, 12)[0]
            text = bytearray()
            for delta in range(256):
                byte = at(name + delta, 1)[0]
                if not byte:
                    break
                text.append(byte)
            else:
                raise ValueError('Unterminated import name')
            decoded = text.decode('ascii').lower()
            require(re.fullmatch(r'[a-z0-9_.-]+\.dll', decoded), 'Invalid import basename')
            imports.add(decoded)
    raise ValueError('Unterminated/oversized import descriptors')


def roots(local_app_data):
    anchor = plain(local_app_data or os.environ['LOCALAPPDATA'], exists=False)
    root = plain(anchor / PRODUCT / 'captures', exists=False)
    return root


def admit(receipt, game, basename, *, verify_source):
    require(receipt.get('schema') == 1 and receipt.get('reviewed') is True and
            all(isinstance(receipt.get(key), dict) and receipt[key] for key in ('source_identity', 'build_identity')),
            'Expected reviewed artifact/source/build receipt')
    artifact = receipt.get('artifact', {})
    require(type(artifact.get('bytes')) is int and 0 < artifact['bytes'] <= 64 * 1024 * 1024 and
            re.fullmatch('[0-9a-f]{64}', artifact.get('sha256', '')), 'Artifact identity missing')
    source = plain(artifact['path'], exists=verify_source)
    game = plain(game)
    require(tuple(part.lower() for part in game.parts[-4:]) == ('end', 'binaries', 'win64', 'ff7rebirth_.exe'),
            'Expected canonical game executable')
    require(basename.lower() in PROFILES and basename.lower() in pe_imports(game), 'Not a supported normal import')
    catalog = read_json(ROOT / 'src/game/rva_catalog.json', 2 * 1024 * 1024)
    builds = [entry for entry in catalog['builds'] if entry['id'] == receipt.get('build_id')]
    require(len(builds) == 1, 'Unknown catalog build')
    build = builds[0]
    require(receipt.get('game_sha256') == build['sha256'] and receipt.get('game_bytes') == build['file_size'],
            'Receipt/game catalog mismatch')
    game_fact = expect(game, {'bytes': build['file_size'], 'sha256': build['sha256']})
    if verify_source:
        expect(source, artifact)
        pe_imports(source, dll=True)
    return game_fact


def checkpoint(session, state, phase, operation, **facts):
    history = state['history'] + [{'operation': operation, **facts}]
    require(len(history) <= 64, 'Journal exceeds bound')
    candidate = {**state, 'phase': phase, 'history': history}
    publish(session.parent / f'checkpoint-{len(history):04}.json', candidate)
    state.update(candidate)  # Never advance memory/history before durable publication.


def load(session_path, local_app_data=None):
    session = plain(session_path)
    root = roots(local_app_data)
    require(session.name == 'session.json' and session.parent.parent == root and
            re.fullmatch(r'native-proxy-[0-9]{8}T[0-9]{6}Z-[0-9a-f]{32}', session.parent.name), 'Unknown session path')
    plan = read_json(session)
    nonce = session.parent.name.rsplit('-', 1)[1]
    require(plan.get('schema') == 1 and plan.get('session_id') == session.parent.name and plan.get('authorized') is True and
            plan.get('phase') == 'planned' and plan.get('history') == [], 'Invalid session plan')
    receipt = read_json(session.parent / 'artifact-receipt.json')
    expect(session.parent / 'artifact-receipt.json', plan['receipt'])
    game = admit(receipt, plan['game']['path'], plan['basename'], verify_source=False)
    require(game == plan['game'] and plan['artifact'] == receipt['artifact'], 'Session identity mismatch')
    require(not root.is_relative_to(Path(game['path']).parents[3]), 'Evidence must be outside game tree')
    directory = Path(game['path']).parent
    expected = {'target': directory / plan['basename'], 'asi': directory / f'{PRODUCT}.asi',
                'asi_backup': directory / f'{PRODUCT}.asi.disabled-native-proxy-{nonce}',
                'log': directory / f'{PRODUCT}.log', 'log_backup': directory / f'{PRODUCT}.log.before-native-proxy-{nonce}',
                'staging': directory / f'.native-proxy-{nonce}.tmp'}
    require(all(plan.get(key) == str(path) for key, path in expected.items()), 'Session paths mismatch')
    for path in expected.values():
        plain(path, exists=False)
    for key in ('asi_before', 'log_before'):
        value = plan.get(key)
        require(value is None or (type(value.get('bytes')) is int and 0 <= value['bytes'] <= FILE_LIMIT and
                re.fullmatch('[0-9a-f]{64}', value.get('sha256', ''))), 'Invalid prior-file identity')
    state = plan.copy()
    files = sorted(session.parent.glob('checkpoint-*.json'))
    require(len(files) <= 64, 'Journal exceeds bound')
    for number, path in enumerate(files, 1):
        require(path.name == f'checkpoint-{number:04}.json', 'Journal gap')
        item = read_json(path)
        require(item.get('phase') in {'planned', 'preparing', 'prepared', 'failed', 'rolling_back', 'rolled_back',
                'collecting', 'collected', 'keeping', 'kept'} and set(item) == set(plan) and
                all(item[key] == plan[key] for key in plan if key not in {'phase', 'history'}) and
                len(item['history']) == number and item['history'][:-1] == state['history'], 'Invalid journal history')
        state = item
    claim = root / ('native-proxy-owner-' + hashlib.sha256(str(directory).lower().encode()).hexdigest() + '.json')
    if state['phase'] != 'rolled_back':
        require(read_json(claim) == {'session': str(session)}, 'Session ownership conflict')
    return session, state, claim


def prepare(receipt_path, game_binary, *, basename='D3DCOMPILER_43.dll', authorize=False,
            process_reader=None, local_app_data=None):
    require(authorize is True, 'Explicit --authorize-prepare required')
    receipt_path = plain(receipt_path)
    receipt = read_json(receipt_path)
    game = admit(receipt, game_binary, basename, verify_source=True)
    closed(process_reader)
    directory = Path(game['path']).parent
    target = plain(directory / basename, exists=False)
    require(not target.exists(), 'Existing proxy must never be overwritten')
    root = roots(local_app_data)
    game_root = Path(game['path']).parents[3]
    require(not root.is_relative_to(game_root) and not receipt_path.is_relative_to(game_root) and
            not Path(receipt['artifact']['path']).is_relative_to(game_root), 'Session, source and receipt must be outside the game tree')
    root.mkdir(parents=True, exist_ok=True)
    claim = root / ('native-proxy-owner-' + hashlib.sha256(str(directory).lower().encode()).hexdigest() + '.json')
    require(not claim.exists(), 'Another proxy session owns this installation; use its receipt')
    name = 'native-proxy-' + time.strftime('%Y%m%dT%H%M%SZ', time.gmtime()) + '-' + uuid.uuid4().hex
    folder = root / name
    folder.mkdir()
    nonce = name.rsplit('-', 1)[1]
    asi = plain(directory / f'{PRODUCT}.asi', exists=False)
    log = plain(directory / f'{PRODUCT}.log', exists=False)
    plan = {'schema': 1, 'session_id': name, 'authorized': True, 'phase': 'planned', 'history': [],
            'game': game, 'basename': basename, 'artifact': receipt['artifact'],
            'receipt': fact(receipt_path, JSON_LIMIT), 'created_ns': time.time_ns(),
            'target': str(target), 'asi': str(asi), 'asi_backup': str(asi) + '.disabled-native-proxy-' + nonce,
            'log': str(log), 'log_backup': str(log) + '.before-native-proxy-' + nonce,
             'staging': str(directory / f'.native-proxy-{nonce}.tmp'),
             'asi_before': fact(asi) if asi.exists() else None, 'log_before': fact(log, 64 * 1024 * 1024) if log.exists() else None}
    for key in ('asi_backup', 'log_backup', 'staging'):
        require(not plain(plan[key], exists=False).exists(), 'Preexisting transaction destination')
    copy_new(receipt_path, folder / 'artifact-receipt.json', plan['receipt'])
    require(read_json(folder / 'artifact-receipt.json') == receipt, 'Receipt changed during preparation')
    for key, filename in (('asi_before', 'asi-before.bin'), ('log_before', 'log-before.log')):
        if plan[key] is not None:
            copy_new(plan[key]['path'], folder / filename, plan[key])
    session = folder / 'session.json'
    publish(session, plan)  # All prior bytes and immutable recovery plan precede game mutations.
    publish(claim, {'session': str(session)})
    state = plan.copy()
    try:
        for key in ('asi', 'log'):
            if plan[key + '_before'] is not None:
                checkpoint(session, state, 'preparing', 'rename_' + key + '_intent')
                closed(process_reader)
                expect(plan[key], plan[key + '_before'])
                rename(plan[key], plan[key + '_backup'])
                checkpoint(session, state, 'preparing', 'rename_' + key + '_done')
        checkpoint(session, state, 'preparing', 'stage_candidate_intent')
        closed(process_reader)
        expect(receipt['artifact']['path'], receipt['artifact'])
        copy_new(receipt['artifact']['path'], plan['staging'], receipt['artifact'])
        checkpoint(session, state, 'preparing', 'stage_candidate_done')
        checkpoint(session, state, 'preparing', 'publish_candidate_intent')
        closed(process_reader)
        expect(plan['staging'], receipt['artifact'])
        rename(plan['staging'], target)
        expect(target, receipt['artifact'])
        require(not asi.exists() and not log.exists(), 'ASI/log reappeared during preparation')
        checkpoint(session, state, 'prepared', 'publish_candidate_done', boundary_ns=time.time_ns())
        return str(session)
    except BaseException as error:
        recovery = 'not_restored'
        try:
            _, persisted, _ = load(session, local_app_data)
            checkpoint(session, persisted, 'failed', 'prepare_failed', error=str(error))
        except BaseException:
            pass
        try:
            rollback(session, process_reader=process_reader, local_app_data=local_app_data)
            recovery = 'rolled_back'
        except BaseException as failure:
            recovery = str(failure)
        raise RuntimeError(f'Prepare failed; session={session}; recovery={recovery}; {error}') from error


def rollback(session_path, *, process_reader=None, local_app_data=None):
    session, state, claim = load(session_path, local_app_data)
    closed(process_reader)
    target, stage = Path(state['target']), Path(state['staging'])
    if state['phase'] == 'rolled_back':
        require(not target.exists() and not Path(state['asi_backup']).exists(), 'Completed rollback no longer matches files')
        if state['asi_before'] is not None:
            expect(state['asi'], state['asi_before'])
        else:
            require(not Path(state['asi']).exists(), 'Unexpected ASI after rollback')
        if claim.exists():
            require(read_json(claim) == {'session': str(session)}, 'Ownership claim changed')
            claim.unlink()
        return {'session': str(session), 'phase': 'rolled_back'}
    if target.exists():
        require(any(item['operation'] == 'publish_candidate_intent' for item in state['history']) and not stage.exists(),
                'Candidate ownership uncertain; no removal/restoration')
        expect(target, state['artifact'])
        checkpoint(session, state, 'rolling_back', 'remove_candidate_intent')
        closed(process_reader)
        expect(target, state['artifact'])
        target.unlink()  # Only the hash-verified owned candidate, never an unknown proxy.
        checkpoint(session, state, 'rolling_back', 'remove_candidate_done')
    asi, backup = Path(state['asi']), Path(state['asi_backup'])
    if state['asi_before'] is not None:
        if backup.exists():
            require(not asi.exists(), 'ASI conflict; no overwrite')
            expect(backup, state['asi_before'])
            checkpoint(session, state, 'rolling_back', 'restore_asi_intent')
            closed(process_reader)
            expect(backup, state['asi_before'])
            rename(backup, asi)
            checkpoint(session, state, 'rolling_back', 'restore_asi_done')
        expect(asi, state['asi_before'])
    else:
        require(not asi.exists() and not backup.exists(), 'Unexpected ASI; no repair')
    log, old_log = Path(state['log']), Path(state['log_backup'])
    if old_log.exists():
        expect(old_log, state['log_before'])
        if not log.exists():
            checkpoint(session, state, 'rolling_back', 'restore_log_intent')
            closed(process_reader)
            rename(old_log, log)
            checkpoint(session, state, 'rolling_back', 'restore_log_done')
        # A new experiment log is never replaced; old bytes remain archived.
    checkpoint(session, state, 'rolled_back', 'rollback_complete')
    if claim.exists():
        require(read_json(claim) == {'session': str(session)}, 'Ownership claim changed')
        claim.unlink()
    return {'session': str(session), 'phase': state['phase'], 'staging_evidence': str(stage) if stage.exists() else None}


def disabled(state):
    expect(state['target'], state['artifact'])
    require(not plain(state['asi'], exists=False).exists(), 'Product ASI is not disabled')
    backup = plain(state['asi_backup'], exists=False)
    if state['asi_before'] is not None:
        expect(backup, state['asi_before'])
    else:
        require(not backup.exists(), 'Unexpected ASI backup')


def collect(session_path, *, process_reader=None, local_app_data=None):
    session, state, _ = load(session_path, local_app_data)
    closed(process_reader)
    require(state['phase'] == 'prepared', 'Collect requires prepared session; no implicit retry/assessment')
    disabled(state)
    current = fact(state['log'], 64 * 1024 * 1024)
    require(current['bytes'] > 0 and Path(state['log']).stat().st_mtime_ns >= state['history'][-1]['boundary_ns'] and
            (state['log_before'] is None or current['sha256'] != state['log_before']['sha256']), 'No fresh changed post-prepare log')
    checkpoint(session, state, 'collecting', 'collect_intent')
    copy_new(state['log'], session.parent / 'log-after.log', current)
    checkpoint(session, state, 'collected', 'collect_done', log=fact(session.parent / 'log-after.log'),
               installed=expect(state['target'], state['artifact']), assessment='Unassessed; manual review required')
    return {'session': str(session), 'phase': state['phase'], 'assessment': 'Unassessed; no playback claim'}


def keep(session_path, manual_report, *, authorize=False, process_reader=None, local_app_data=None):
    require(authorize is True, 'Explicit --authorize-keep required')
    session, state, _ = load(session_path, local_app_data)
    closed(process_reader)
    require(state['phase'] == 'collected' and state['history'][-1]['operation'] == 'collect_done', 'Keep requires successful collection')
    disabled(state)
    collection = state['history'][-1]
    require(collection['log']['path'] == str(session.parent / 'log-after.log'), 'Collection path mismatch')
    expect(session.parent / 'log-after.log', collection['log'])
    expect(state['log'], collection['log'])
    report = fact(manual_report, 64 * 1024)
    with plain(manual_report).open('rb') as stream:
        raw = stream.read(64 * 1024 + 1)
    require(0 < len(raw) == report['bytes'] <= 64 * 1024 and hashlib.sha256(raw).hexdigest() == report['sha256'] and
            raw.decode('utf-8').strip(), 'Empty/changed manual review')
    checkpoint(session, state, 'keeping', 'keep_intent', manual_report=report)
    copy_new(manual_report, session.parent / 'manual-report.txt', report)
    checkpoint(session, state, 'kept', 'keep_reviewed', manual_report=fact(session.parent / 'manual-report.txt'),
               scope='Reviewed Debug disposition only; not Release/playback qualification')
    return {'session': str(session), 'phase': state['phase']}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action', required=True)
    start = sub.add_parser('prepare')
    start.add_argument('--artifact-receipt', type=Path, required=True)
    start.add_argument('--game-binary', type=Path, required=True)
    start.add_argument('--basename', default='D3DCOMPILER_43.dll', choices=sorted(PROFILES), type=str.lower)
    start.add_argument('--authorize-prepare', action='store_true', required=True)
    for action in ('collect', 'rollback', 'keep'):
        command = sub.add_parser(action)
        command.add_argument('--session', type=Path, required=True)
        if action == 'keep':
            command.add_argument('--manual-report', type=Path, required=True)
            command.add_argument('--authorize-keep', action='store_true', required=True)
    args = parser.parse_args(argv)
    if args.action == 'prepare':
        result = prepare(args.artifact_receipt, args.game_binary, basename=args.basename, authorize=args.authorize_prepare)
    elif args.action == 'keep':
        result = keep(args.session, args.manual_report, authorize=args.authorize_keep)
    else:
        result = {'collect': collect, 'rollback': rollback}[args.action](args.session)
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
