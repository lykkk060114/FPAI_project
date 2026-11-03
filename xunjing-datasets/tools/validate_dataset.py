#!/usr/bin/env python3
"""Validate YOLO and ImageFolder datasets without modifying training data."""
import argparse
import collections
import csv
import hashlib
import json
import math
from pathlib import Path
import time
from PIL import Image, ImageOps

SUFFIXES = {'.jpg', '.jpeg', '.png'}
CLASSES = {'drink': 0, 'eat': 1, 'rest': 2, 'walk': 3}

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def validate(root):
    started = time.perf_counter()
    root = Path(root).resolve()
    provenance = {}
    pf = root / 'metadata/provenance.jsonl'
    if pf.exists():
        for line in pf.read_text(encoding='utf-8').splitlines():
            record = json.loads(line)
            if record['path'] in provenance:
                raise ValueError('Duplicate provenance path: ' + record['path'])
            provenance[record['path']] = record
    errors, rows = [], []
    detection, classification = {}, {}
    sha_groups, pixel_groups, source_groups = [collections.defaultdict(list) for _ in range(3)]
    task_groups = collections.defaultdict(lambda: collections.defaultdict(list))
    names_casefold = collections.defaultdict(list)

    def image_record(path, task, split, label_path=None, class_name='', boxes=None):
        rel = path.relative_to(root).as_posix()
        try:
            with Image.open(path) as im:
                im.load()
                width, height, mode = im.width, im.height, im.mode
                # Apply EXIF orientation only in memory for duplicate detection.
                rgb = ImageOps.exif_transpose(im).convert('RGB')
                pixel = hashlib.sha256(f'{rgb.width}x{rgb.height}:RGB:'.encode() + rgb.tobytes()).hexdigest()
                oriented = im.getexif().get(274, 1)
        except Exception as exc:
            errors.append({'path': rel, 'error': 'image_decode', 'detail': str(exc)})
            return
        if task == 'classification' and ((width, height) != (224, 224) or mode != 'RGB'):
            errors.append({'path': rel, 'error': 'classification_image_format', 'size': [width, height], 'mode': mode})
        if oriented != 1:
            errors.append({'path': rel, 'error': 'nontrivial_exif_orientation', 'value': oriented})
        prov = provenance.get(rel, {})
        if not prov:
            errors.append({'path': rel, 'error': 'missing_provenance'})
        group = prov.get('source_group', '')
        sha = digest(path)
        row = {'path': rel, 'task': task, 'split': split, 'class_name': class_name,
               'class_id': CLASSES[class_name] if class_name else '',
               'width': width, 'height': height, 'mode': mode, 'bytes': path.stat().st_size,
               'sha256': sha, 'pixel_sha256': pixel,
               'label_path': label_path.relative_to(root).as_posix() if label_path else '',
               'label_sha256': digest(label_path) if label_path and label_path.exists() else '',
               'box_count': len(boxes) if boxes is not None else '',
               'negative': str(not boxes).lower() if boxes is not None else '',
               'source': prov.get('source', ''), 'source_group': group,
               'source_url': prov.get('source_url', ''), 'license_status': prov.get('license_status', '')}
        if prov.get('exported_sha256') and prov['exported_sha256'] != sha:
            errors.append({'path': rel, 'error': 'provenance_hash_mismatch'})
        rows.append(row)
        sha_groups[sha].append(rel)
        pixel_groups[pixel].append(rel)
        if group:
            source_groups[group].append(rel)
            task_groups[task][group].append((split, rel))
        names_casefold[rel.casefold()].append(rel)

    for split in ('train', 'val', 'test'):
        image_dir = root / 'detection/images' / split
        label_dir = root / 'detection/labels' / split
        images = sorted(p for p in image_dir.iterdir() if p.is_file() and p.suffix.lower() in SUFFIXES) if image_dir.exists() else []
        if not images:
            errors.append({'path': str(image_dir.relative_to(root)), 'error': 'missing_or_empty_detection_split'})
        counts = {'images': len(images), 'boxes': 0, 'negative_images': 0}
        image_stems = collections.Counter(p.stem for p in images)
        for stem, count in image_stems.items():
            if count > 1: errors.append({'path': str(image_dir / stem), 'error': 'duplicate_image_stem'})
        for path in images:
            label = label_dir / (path.stem + '.txt')
            boxes = []
            if not label.exists():
                errors.append({'path': str(label.relative_to(root)), 'error': 'missing_label'})
            else:
                for line_num, line in enumerate(label.read_text(encoding='utf-8').splitlines(), 1):
                    if not line.strip(): continue
                    fields = line.split()
                    if len(fields) != 5:
                        errors.append({'path': str(label.relative_to(root)), 'line': line_num, 'error': 'yolo_field_count'}); continue
                    try:
                        cls, x, y, w, h = map(float, fields)
                        valid = cls == 0 and all(map(math.isfinite, (cls, x, y, w, h))) and 0 <= x <= 1 and 0 <= y <= 1 and 0 < w <= 1 and 0 < h <= 1
                        valid = valid and x - w/2 >= -1e-6 and y - h/2 >= -1e-6 and x + w/2 <= 1+1e-6 and y + h/2 <= 1+1e-6
                    except ValueError: valid = False
                    if not valid:
                        errors.append({'path': str(label.relative_to(root)), 'line': line_num, 'error': 'invalid_yolo_box', 'value': line})
                    else: boxes.append(fields)
            counts['boxes'] += len(boxes)
            counts['negative_images'] += int(not boxes)
            image_record(path, 'detection', split, label, boxes=boxes)
        for label in sorted(label_dir.glob('*.txt')):
            if label.stem not in image_stems:
                errors.append({'path': str(label.relative_to(root)), 'error': 'orphan_label'})
        detection[split] = counts

    for split in ('train', 'val'):
        classification[split] = {}
        split_dir = root / 'classification' / split
        for child in split_dir.iterdir() if split_dir.exists() else []:
            if child.is_dir() and child.name not in CLASSES:
                errors.append({'path': str(child.relative_to(root)), 'error': 'unknown_class_directory'})
        for name in CLASSES:
            directory = split_dir / name
            images = sorted(p for p in directory.iterdir() if p.is_file() and p.suffix.lower() in SUFFIXES) if directory.exists() else []
            if not images:
                errors.append({'path': str(directory.relative_to(root)), 'error': 'missing_or_empty_class_directory'})
            classification[split][name] = len(images)
            for path in images: image_record(path, 'classification', split, class_name=name)
    def duplicate_report(groups):
        return [paths for paths in groups.values() if len(paths) > 1]
    def split_of(path):
        return path.split('/')[2] if path.startswith('detection/') else path.split('/')[1]
    within_task_leaks = []
    for paths in duplicate_report(pixel_groups):
        for task in ('detection', 'classification'):
            subset = [p for p in paths if p.startswith(task + '/')]
            if len({split_of(p) for p in subset}) > 1:
                within_task_leaks.append({'task': task, 'paths': subset})
    group_leaks = []
    for task, groups in task_groups.items():
        for group, records in groups.items():
            if len({s for s, _ in records}) > 1:
                group_leaks.append({'task': task, 'source_group': group, 'paths': [p for _, p in records]})
    for paths in names_casefold.values():
        if len(paths) > 1: errors.append({'error': 'case_insensitive_path_collision', 'paths': paths})
    expected = {r['path'] for r in rows}
    for path in sorted(set(provenance) - expected): errors.append({'path': path, 'error': 'orphan_provenance'})
    if within_task_leaks: errors.append({'error': 'cross_split_pixel_duplicates', 'groups': len(within_task_leaks)})
    if group_leaks: errors.append({'error': 'cross_split_source_group_overlap', 'groups': len(group_leaks)})
    report = {'schema_version': 1, 'ok': not errors, 'detection': detection, 'classification': classification,
              'total_images': len(rows), 'image_bytes': sum(r['bytes'] for r in rows),
              'largest_image_bytes': max((r['bytes'] for r in rows), default=0),
              'source_counts': dict(collections.Counter(r['source'] for r in rows)),
              'exact_byte_duplicate_groups': duplicate_report(sha_groups),
              'exact_pixel_duplicate_groups': duplicate_report(pixel_groups),
              'within_task_cross_split_pixel_duplicates': within_task_leaks,
              'within_task_cross_split_source_groups': group_leaks,
              'errors': errors, 'elapsed_seconds': round(time.perf_counter()-started, 6),
              'scope': 'Decodability, dimensions, labels, hashes and recorded source groups. Does not certify semantic label accuracy or independent scenes across unknown reuploads.'}
    metadata = root / 'metadata'; metadata.mkdir(exist_ok=True)
    (metadata / 'audit.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    with (metadata / 'manifest.csv').open('w', newline='', encoding='utf-8') as out:
        if rows:
            writer = csv.DictWriter(out, fieldnames=list(rows[0])); writer.writeheader(); writer.writerows(rows)
    checksum_paths = sorted(root.glob('detection/images/*/*')) + sorted(root.glob('detection/labels/*/*.txt')) + sorted(root.glob('classification/train/*/*')) + sorted(root.glob('classification/val/*/*'))
    (metadata / 'SHA256SUMS').write_text(''.join(f'{digest(p)}  {p.relative_to(root).as_posix()}\n' for p in checksum_paths if p.is_file()), encoding='utf-8')
    return report

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    args = parser.parse_args()
    report = validate(args.root)
    summary = {k: report[k] for k in ('ok', 'detection', 'classification', 'total_images', 'image_bytes', 'source_counts', 'elapsed_seconds')}
    summary.update(error_count=len(report['errors']), pixel_duplicate_groups=len(report['exact_pixel_duplicate_groups']), cross_split_source_groups=len(report['within_task_cross_split_source_groups']))
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    if report['errors']: print(json.dumps(report['errors'][:20], ensure_ascii=False, indent=2))
    raise SystemExit(0 if report['ok'] else 1)

if __name__ == '__main__': main()
