"""Apply or roll back the staged, verified RSMods intro asset experiment."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from rocksmith_diag import WindowsProcess, utc_now
ROOT=Path(__file__).resolve().parents[1]
PROTECTED=('Rocksmith2014.exe','RS_ASIO.dll','RS_ASIO.ini','avrt.dll','D3DX9_42.dll','xinput1_3.dll','RSMods.ini','Rocksmith.ini','dinput8.dll','RSStartup.ini')
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def write(path,value):path.write_text(json.dumps(value,indent=2),encoding='utf-8')
def replace_checked(source,target,old_hash):
    temp=target.with_name(target.name+'.rsmodernized.tmp')
    if temp.exists():raise RuntimeError('Existing temporary archive; inspect before retrying')
    if digest(target)!=old_hash:raise RuntimeError('Game archive changed; refusing replacement')
    data=source.read_bytes()
    try:
        with temp.open('xb') as f:
            f.write(data);f.flush();os.fsync(f.fileno())
        if digest(temp)!=hashlib.sha256(data).hexdigest():raise RuntimeError('Temporary archive hash mismatch')
        if digest(target)!=old_hash:raise RuntimeError('Game archive changed during staging')
        os.replace(temp,target)
    finally:
        if temp.exists() and digest(temp)==hashlib.sha256(data).hexdigest():temp.unlink()
def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('action',choices=('install','rollback'))
    parser.add_argument('--game',type=Path,required=True)
    parser.add_argument('--stage',type=Path,required=True,help='Verified experiment directory inside reports')
    parser.add_argument('--archive',choices=('cache.psarc','static.psarc'),default='cache.psarc')
    args=parser.parse_args()
    stage=args.stage.resolve()
    if not stage.is_relative_to((ROOT/'reports').resolve()):raise RuntimeError('Stage must be inside workspace reports')
    manifest=stage/'installation.json'
    game=args.game.resolve();target=game/args.archive
    protected=PROTECTED+(('cache.psarc',) if args.archive=='static.psarc' else ())
    if target.resolve().parent!=game:raise RuntimeError('Unsafe archive target')
    api=WindowsProcess();found=api.find(game)
    if found:
        api.k.CloseHandle(found[0]);raise SystemExit('Close the game first; no files changed')
    verification=json.loads((stage/'outer-verification.json').read_text(encoding='utf-8'))
    nested=json.loads((stage/'nested-verification.json').read_text(encoding='utf-8'))
    if args.archive=='cache.psarc':
        assert set(verification['changed_entries'])=={'NamesBlock.bin','cache4.7z'}
        assert verification['added_entries']==['sltsv1_aggregategraph.nt'] and not verification['removed_entries']
        assert nested['changed_entries']==[r'gfxassets\views\introsequence.gfx']
    else:
        assert verification['changed_entries']==['gfxassets/views/introsequence.gfx']
        assert not verification['added_entries'] and not verification['removed_entries']
        assert nested['changed_entries']==['gfxassets/views/introsequence.gfx']
    assert nested['selected_intro_sha256']=='b985a2c7eef68cfedb924ddcd258a78f22c1625c6bce6428b10d33f15ec06de9'
    for file,key in [('original.psarc','original_sha256'),('candidate.psarc','candidate_sha256')]:
        if digest(stage/file)!=verification[key]:raise RuntimeError('Staged archive changed: '+file)
    if args.action=='install':
        if manifest.exists():raise SystemExit('Installation record already exists; refusing another installation')
        m=dict(game=str(game),archive=args.archive,installed_utc=utc_now(),status='installing',original_sha256=verification['original_sha256'],candidate_sha256=verification['candidate_sha256'],protected_before={n:digest(game/n)for n in protected})
        write(manifest,m)
        try:
            replace_checked(stage/'candidate.psarc',target,m['original_sha256'])
            if digest(target)!=m['candidate_sha256']:raise RuntimeError('Installed archive hash mismatch')
            m['protected_after']={n:digest(game/n)for n in protected}
            if m['protected_before']!=m['protected_after']:raise RuntimeError('Protected file changed')
            m['status']='installed_verified'
        except Exception:
            if digest(target)==m['candidate_sha256']:
                replace_checked(stage/'original.psarc',target,m['candidate_sha256']);m['status']='failed_restored_original'
            else:m['status']='failed_inspect_archive_before_retry'
            raise
        finally:write(manifest,m)
    else:
        m=json.loads(manifest.read_text(encoding='utf-8'))
        if m['game']!=str(game)or m.get('archive','cache.psarc')!=args.archive or m['status']!='installed_verified':raise RuntimeError('Invalid installation record')
        replace_checked(stage/'original.psarc',target,m['candidate_sha256'])
        if digest(target)!=m['original_sha256']:raise RuntimeError('Restored archive hash mismatch')
        m.update(status='rolled_back',rolled_back_utc=utc_now());write(manifest,m)
    print(m['status'])
if __name__=='__main__':main()
