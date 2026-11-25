"""Resume-safe rollback of the original profile-adapter experiment record.

New installations use startup_install.py. This preserves rollback of the older
record format without selecting a machine path or dated report implicitly.
"""
import argparse
import json
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from rocksmith_diag import utc_now
from tools.apply_intro import replace_checked
from tools.startup_install import check_closed, check_target, digest, write_json


def rollback(game, stage, base_record):
    game, stage = game.resolve(), stage.resolve()
    check_closed(game)
    manifest = stage / 'installation.json'
    value = json.loads(manifest.read_text(encoding='utf-8'))
    if value['status'] not in ('installed_verified', 'rolling_back', 'rolled_back'):
        raise RuntimeError('Invalid installation status')
    if set(value['before']) != {'dinput8.dll', 'RSStartup.ini'} or set(value['after']) != set(value['before']):
        raise RuntimeError('Invalid file set')
    base = json.loads(base_record.read_text(encoding='utf-8'))
    if Path(base['game']).resolve() != game:
        raise RuntimeError('Base record belongs to another installation')
    for name in value['before']:
        if (digest(check_target(game, name)) not in (value['before'][name], value['after'][name])
                or digest(stage / 'previous' / name) != value['before'][name]):
            raise RuntimeError('File changed: ' + name)
    value['status'] = 'rolling_back'
    write_json(manifest, value)
    for name in value['before']:
        if digest(game / name) != value['before'][name]:
            replace_checked(stage / 'previous' / name, game / name, value['after'][name])
    base['files'].update(value['before'])
    write_json(base_record, base)
    value.update(status='rolled_back', rolled_back_utc=utc_now())
    write_json(manifest, value)
    return value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=('rollback',))
    parser.add_argument('--game', type=Path, required=True)
    parser.add_argument('--stage', type=Path, required=True)
    parser.add_argument('--base-record', type=Path, required=True)
    args = parser.parse_args()
    print(rollback(args.game, args.stage, args.base_record)['status'])


if __name__ == '__main__':
    main()
