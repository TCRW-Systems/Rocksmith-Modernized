"""Journaled installation of the startup proxy; no machine-specific defaults.

Install takes an explicit game, DLL, INI and new record directory. Resume and
rollback use that immutable record. Interrupted operations accept each file in
either recorded state and refuse unrelated edits before changing any file.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from rocksmith_diag import WindowsProcess, utc_now
from tools.apply_intro import replace_checked

NAMES = ('dinput8.dll', 'RSStartup.ini')
PROTECTED = ('Rocksmith2014.exe', 'RS_ASIO.dll', 'RS_ASIO.ini', 'avrt.dll',
             'D3DX9_42.dll', 'xinput1_3.dll', 'RSMods.ini', 'Rocksmith.ini',
             'cache.psarc', 'static.psarc')


def digest(path):
    if path.is_symlink():
        raise RuntimeError('Refusing symbolic link: ' + str(path))
    if not path.exists():
        return None
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def write_json(path, value):
    temp = path.with_name(path.name + '.' + uuid.uuid4().hex + '.tmp')
    try:
        with temp.open('x', encoding='utf-8') as stream:
            json.dump(value, stream, indent=2)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temp, path)
    finally:
        temp.unlink(missing_ok=True)


def check_closed(game):
    api = WindowsProcess()
    found = api.find(game)
    if found:
        api.k.CloseHandle(found[0])
        raise RuntimeError('Close Rocksmith first; no files changed')


def check_target(game, name):
    target = game / name
    if target.is_symlink() or target.resolve().parent != game:
        raise RuntimeError('Unsafe installation target: ' + str(target))
    return target


def prepare(game, record, dll, config, replace_existing=False):
    game, record = game.resolve(), record.resolve()
    if not (game / 'Rocksmith2014.exe').is_file():
        raise RuntimeError('Selected directory has no Rocksmith2014.exe')
    if record == game or record.is_relative_to(game):
        raise RuntimeError('Keep the installation record outside the game directory')
    check_closed(game)
    before = {name: digest(check_target(game, name)) for name in NAMES}
    if any(before.values()) and not replace_existing:
        raise RuntimeError('Existing proxy/config; inspect them and use --replace-existing for an intentional update')
    content = {'dinput8.dll': dll.read_bytes(), 'RSStartup.ini': config.read_bytes()}
    if any(not data for data in content.values()):
        raise RuntimeError('Empty installation input')
    protected = {name: digest(check_target(game, name)) for name in PROTECTED}
    record.mkdir(parents=True, exist_ok=False)
    (record / 'before').mkdir()
    (record / 'after').mkdir()
    for name in NAMES:
        if before[name] is not None:
            (record / 'before' / name).write_bytes((game / name).read_bytes())
            if digest(record / 'before' / name) != before[name]:
                raise RuntimeError('Source changed during backup: ' + name)
        (record / 'after' / name).write_bytes(content[name])
    value = dict(schema=1, game=str(game), status='prepared', created_utc=utc_now(),
                 before=before, after={n: digest(record / 'after' / n) for n in NAMES},
                 protected_before=protected)
    write_json(record / 'installation.json', value)
    return value


def create_checked(source, target, expected):
    # Hard-link creation refuses an existing destination atomically on NTFS.
    temp = target.with_name(target.name + '.' + uuid.uuid4().hex + '.tmp')
    try:
        with temp.open('xb') as stream:
            stream.write(source.read_bytes())
            stream.flush()
            os.fsync(stream.fileno())
        if digest(temp) != expected:
            raise RuntimeError('Staged input changed')
        os.link(temp, target)
    finally:
        temp.unlink(missing_ok=True)


def transition(record, rollback=False):
    record = record.resolve()
    path = record / 'installation.json'
    value = json.loads(path.read_text(encoding='utf-8'))
    if value.get('schema') != 1 or set(value['before']) != set(NAMES) or set(value['after']) != set(NAMES):
        raise RuntimeError('Unsupported installation record')
    game = Path(value['game']).resolve()
    check_closed(game)
    if value['status'] == 'rolled_back' and not rollback:
        raise RuntimeError('This installation was rolled back; prepare a new record')
    if value['status'] == 'rolling_back' and not rollback:
        raise RuntimeError('Rollback in progress; resume rollback')
    desired = value['before' if rollback else 'after']
    stage = record / ('before' if rollback else 'after')
    # Preflight every target before the first mutation, including on retries.
    for name in NAMES:
        actual = digest(check_target(game, name))
        if actual not in (value['before'][name], value['after'][name]):
            raise RuntimeError('File changed outside this installation: ' + name)
        if desired[name] is not None and digest(stage / name) != desired[name]:
            raise RuntimeError('Backup/candidate changed: ' + name)
    if not rollback:
        current = {n: digest(check_target(game, n)) for n in PROTECTED}
        if current != value['protected_before']:
            raise RuntimeError('Protected files changed; no files replaced')
    value['status'] = 'rolling_back' if rollback else 'installing'
    write_json(path, value)
    for name in NAMES:
        target = check_target(game, name)
        actual = digest(target)
        if actual == desired[name]:
            continue
        if desired[name] is None:
            if digest(target) != value['after'][name]:
                raise RuntimeError('File changed before removal: ' + name)
            target.unlink()
        elif actual is None:
            create_checked(stage / name, target, desired[name])
        else:
            replace_checked(stage / name, target, actual)
        if digest(target) != desired[name]:
            raise RuntimeError('File verification failed: ' + name)
        value['last_completed'] = name
        write_json(path, value)
    if not rollback:
        value['protected_after'] = {n: digest(game / n) for n in PROTECTED}
        if value['protected_after'] != value['protected_before']:
            raise RuntimeError('Protected files changed during installation; inspect and roll back')
    value.update(status='rolled_back' if rollback else 'installed_verified', updated_utc=utc_now())
    write_json(path, value)
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='action', required=True)
    install = sub.add_parser('install')
    install.add_argument('--game', type=Path, required=True)
    install.add_argument('--dll', type=Path, required=True)
    install.add_argument('--config', type=Path, required=True)
    install.add_argument('--replace-existing', action='store_true')
    for command in (install, sub.add_parser('resume'), sub.add_parser('rollback')):
        command.add_argument('--record', type=Path, required=True)
    args = parser.parse_args()
    if args.action == 'install':
        prepare(args.game, args.record, args.dll, args.config, args.replace_existing)
    print(transition(args.record, args.action == 'rollback')['status'])


if __name__ == '__main__':
    main()
