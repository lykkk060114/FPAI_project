#!/usr/bin/env python3
"""Export the curated FMQL datasets, preserving all sample bytes and splits."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import shutil
import time
from validate_dataset import validate

CLASSES = {'drink': 0, 'eat': 1, 'rest': 2, 'walk': 3}

def read_json(path):
    return json.loads(path.read_text(encoding='utf-8'))

def export(source, dest):
    start = time.perf_counter()
    source, dest = Path(source).resolve(), Path(dest).resolve()
    if source == dest or source in dest.parents or dest in source.parents:
        raise ValueError('Use a separate destination outside the source workspace.')
    if (dest/'metadata/provenance.jsonl').exists():
        raise ValueError('Destination already contains a dataset; use a fresh directory.')
    archive = source / 'dataset_sources_archive'
    metadata = dest/'metadata'; metadata.mkdir(parents=True, exist_ok=True)
    records = {}
    def add(rel, source_name, group, url, license_status, detail):
        if rel in records: raise ValueError('Duplicate provenance entry: '+rel)
        records[rel] = {'path': rel, 'source': source_name, 'source_group': group,
                        'source_url': url, 'license_status': license_status, 'details': detail}
    # The derived LoTE split covers every public detection image used here.
    with (archive/'derived_metadata/detection/manifest.csv').open(encoding='utf-8', newline='') as file:
        for row in csv.DictReader(file):
            rel = f"detection/images/{row['derived_split']}/{row['file_name']}"
            add(rel, 'lote_animal', 'lote:image:'+Path(row['file_name']).stem,
                'https://lote-animal.github.io/', 'No explicit dataset redistribution license recorded',
                dict(row, grouping='Image identity only; original scene/video grouping unavailable'))
    for file in sorted((source/'datasets/camera_recordings').glob('annotations_*.json')):
        for row in read_json(file)['records']:
            rel=f"detection/images/{row.get('split','train')}/{row['image']}"
            add(rel, 'camera_recordings', 'camera:'+row['batch'], '',
                'Project camera capture; rights to displayed panda image are not separately established' if 'background' not in row['image'] else 'Project camera capture; no public data license assigned',
                dict(row, kind='background_negative' if 'background' in row['image'] else 'screen_recapture', annotation_manifest=file.name))
    # Historical DeepPanda CSV class_id belongs to an older mapping.
    # Recompute the released class_id from the actual ImageFolder class name.
    with (archive/'derived_metadata/classification/manifest.csv').open(encoding='utf-8', newline='') as file:
        for row in csv.DictReader(file):
            rel = 'classification/'+row['crop_path']
            detail=dict(row)
            detail['historical_class_id']=detail.pop('class_id')
            detail['class_id']=CLASSES[row['class_name']]
            add(rel, 'deep_panda', 'deep_panda:video:'+row['video_id'],
                'https://github.com/RedBean17/DeepPanda', 'Source repository has no LICENSE recorded', detail)
    specs = [
        ('wikimedia_commons', 'classification/wikimedia_commons_behavior/metadata/installed_manifest.json'),
        ('lote_behavior', 'classification/lote_behavior_supplement/metadata/installed_manifest.json'),
        ('online_behavior_videos', 'classification/online_behavior_videos/installed_manifest.json'),
        ('supplement_20261027', 'classification/supplement_20261027/installed_manifest.json'),
    ]
    for name, relative in specs:
        for row in read_json(archive/relative):
            rel='classification/'+row['destination']
            if name=='wikimedia_commons':
                group='commons:'+row.get('group',row['id'])
                url=row.get('page_url','')
                license_status=row.get('license','Unknown')
            elif name=='lote_behavior':
                group='lote:image:'+Path(row['source_image']).stem
                url=row['source_url'];license_status=row['license_status']
            else:
                group='youtube:'+row['video_id']
                url=row['source_url'];license_status=row.get('license','Unknown')
            add(rel,name,group,url,license_status,dict(row))
    # Copy only files actually consumed by training: no videos, backups, raw archives or caches.
    files=[]
    for glob in ('detection/images/train/*','detection/images/val/*','detection/images/test/*',
                 'detection/labels/train/*.txt','detection/labels/val/*.txt','detection/labels/test/*.txt',
                 'classification/train/*/*','classification/val/*/*'):
        for file in sorted((source/'datasets').glob(glob)):
            if not file.is_file(): continue
            if '/images/' in glob or glob.startswith('classification/'):
                if file.suffix.lower() not in {'.jpg','.jpeg','.png'}:continue
            relative=file.relative_to(source/'datasets')
            output=dest/relative;output.parent.mkdir(parents=True,exist_ok=True)
            if output.exists():raise ValueError('Refusing to overwrite '+str(output))
            shutil.copy2(file,output)
            sha=hashlib.sha256(file.read_bytes()).hexdigest()
            assert hashlib.sha256(output.read_bytes()).hexdigest()==sha
            rel=relative.as_posix()
            if rel in records:records[rel]['exported_sha256']=sha
            files.append(output)
    image_paths={file.relative_to(dest).as_posix() for file in files if file.suffix.lower() in {'.jpg','.jpeg','.png'}}
    if image_paths != set(records):
        raise ValueError({'missing_provenance':sorted(image_paths-set(records)), 'missing_images':sorted(set(records)-image_paths)})
    (metadata/'provenance.jsonl').write_text(''.join(json.dumps(records[k],ensure_ascii=False)+'\n' for k in sorted(records)),encoding='utf-8')
    (dest/'detection/data.yaml').write_text('train: images/train\nval: images/val\ntest: images/test\nnc: 1\nnames:\n  0: giant_panda\n',encoding='utf-8')
    (dest/'detection/classes.txt').write_text('giant_panda\n',encoding='utf-8')
    (dest/'classification/dataset.yaml').write_text('train: train\nval: val\nnc: 4\nnames:\n  0: drink\n  1: eat\n  2: rest\n  3: walk\n',encoding='utf-8')
    (dest/'classification/classes.txt').write_text('drink\neat\nrest\nwalk\n',encoding='utf-8')
    (dest/'classification/class_indices.json').write_text(json.dumps({'class_to_idx':CLASSES,'idx_to_class':{str(v):k for k,v in CLASSES.items()}},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    # Include source derivation methods, not raw data or obsolete local absolute paths.
    methods={
        'detection_split':read_json(archive/'derived_metadata/detection/split_report.json'),
        'deep_panda_derivation':read_json(archive/'derived_metadata/classification/report.json'),
        'classification_mapping_note':'Historical crop CSV class_id differs from current training mapping. The release uses directory names and class_indices.json: drink=0,eat=1,rest=2,walk=3.',
        'camera_note':'Camera panda images are screen recaptures, not direct captures of live pandas. The capture timestamps are device clock values and not reliable calendar dates.',
        'split_policy':'Preserve existing sample split assignments exactly. Camera recaptures and online video supplements remain train-only.',
    }
    (metadata/'derivation.json').write_text(json.dumps(methods,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    report=validate(dest)
    for task in ('detection','classification'):
        (dest/task/'dataset_stats.json').write_text(json.dumps({'task':task,'counts':report[task]},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    preparation={'copied_files':len(files),'copied_bytes':sum(p.stat().st_size for p in files),
                 'source_bytes_preserved':True,'split_assignments_preserved':True,
                 'preparation_elapsed_seconds':round(time.perf_counter()-start,6),
                 'audit_ok':report['ok']}
    (metadata/'preparation.json').write_text(json.dumps(preparation,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(preparation,ensure_ascii=False,indent=2))
    print('Audit errors:',json.dumps(report['errors'][:20],ensure_ascii=False))
    print('Pixel duplicate groups:',len(report['exact_pixel_duplicate_groups']))
    print('Source group split overlaps:',len(report['within_task_cross_split_source_groups']))
    if not report['ok']:raise SystemExit(1)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',required=True,type=Path,help='FMQL workspace containing datasets and dataset_sources_archive')
    parser.add_argument('--output',required=True,type=Path,help='Fresh standalone export directory')
    args=parser.parse_args();export(args.source,args.output)
if __name__=='__main__':main()
