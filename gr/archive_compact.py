#!/usr/bin/env python3
"""archive_compact.py -- shrink the run archive (~/Code/GlobalRouterRuns) without losing a result.

1. Hash the uncompressed content of every solution file (*.route, *.g24sol, and their .gz) under
   runs/ and la_from/ -- resumable: hashes go to ARCHIVE_HASHES.tsv as they are computed.
2. Duplicates (same content hash): keep the earliest file, delete the others. Every deletion is
   written to ARCHIVE_MANIFEST.tsv first (removed path, hash, kept path), so a summary's route can
   always be found.
3. Re-compress every kept file to .xz (xz -6): the new file is decompressed and re-hashed before the
   old one is removed. The manifest's kept paths are updated to the .xz names.

Files of runs still in progress (a .stderr.tmp beside them, or modified in the last 3 hours) are left
alone. Read archived routes with `xz -dc` (or `gunzip -c` for ones not yet converted).

  nice -n 19 python3 gr/archive_compact.py [--dry-run]
"""
import hashlib, os, subprocess, sys, time

ROOT = os.path.expanduser('~/Code/GlobalRouterRuns')
HASHES = os.path.join(ROOT, 'ARCHIVE_HASHES.tsv')
MANIFEST = os.path.join(ROOT, 'ARCHIVE_MANIFEST.tsv')
LOG = os.path.join(ROOT, 'ARCHIVE_COMPACT.log')
SUFFIXES = ('.route', '.g24sol', '.route.gz', '.g24sol.gz')
DRY = '--dry-run' in sys.argv


def log(msg):
    line = time.strftime('%Y-%m-%d %H:%M:%S ') + msg
    print(line, flush=True)
    with open(LOG, 'a') as f:
        f.write(line + '\n')


def stem(path):
    for s in ('.gz', '.xz'):
        if path.endswith(s):
            path = path[:-len(s)]
    for s in ('.route', '.g24sol'):
        if path.endswith(s):
            return path[:-len(s)]
    return path


def in_progress(path):
    st = stem(path)
    return os.path.exists(st + '.stderr.tmp') or time.time() - os.path.getmtime(path) < 3 * 3600


def content_hash(path, xz=False):
    """md5 of the uncompressed content; xz=True reads `path` as xz whatever its name (the .xz.tmp)."""
    h = hashlib.md5()
    if path.endswith('.gz'):
        p = subprocess.Popen(['gunzip', '-c', path], stdout=subprocess.PIPE)
        src = p.stdout
    elif xz or path.endswith('.xz'):
        p = subprocess.Popen(['xz', '-dc', path], stdout=subprocess.PIPE)
        src = p.stdout
    else:
        p, src = None, open(path, 'rb')
    while True:
        b = src.read(1 << 22)
        if not b:
            break
        h.update(b)
    if p:
        if p.wait() != 0:
            raise RuntimeError('decompress failed: ' + path)
    else:
        src.close()
    return h.hexdigest()


def candidates():
    out = []
    for top in ('runs', 'la_from'):
        for d, _, files in os.walk(os.path.join(ROOT, top)):
            for f in files:
                p = os.path.join(d, f)
                if f.endswith(SUFFIXES) and not os.path.islink(p) and not in_progress(p):
                    out.append(p)
    return sorted(out)


def main():
    known = {}
    if os.path.exists(HASHES):
        for line in open(HASHES):
            p, size, mtime, h = line.rstrip('\n').split('\t')
            known[p] = (int(size), float(mtime), h)
    files = candidates()
    log(f'start{" (dry run)" if DRY else ""}: {len(files)} files, {len(known)} already hashed')
    # 1. hashes
    for i, p in enumerate(files):
        st = os.stat(p)
        k = known.get(p)
        if k and k[0] == st.st_size and k[1] == st.st_mtime:
            continue
        h = content_hash(p)
        known[p] = (st.st_size, st.st_mtime, h)
        with open(HASHES, 'a') as f:
            f.write(f'{p}\t{st.st_size}\t{st.st_mtime}\t{h}\n')
        if i % 25 == 0:
            log(f'hashed {i + 1}/{len(files)}')
    # 2. duplicates: keep the earliest file of each content (an archived .gz before a raw copy)
    groups = {}
    for p in files:
        groups.setdefault(known[p][2], []).append(p)
    freed = 0
    kept = []
    for h, ps in groups.items():
        ps.sort(key=lambda p: (not p.startswith(os.path.join(ROOT, 'runs')), not p.endswith('.gz'),
                               known[p][1], p))
        keep = ps[0]
        kept.append(keep)
        for p in ps[1:]:
            freed += known[p][0]
            if not DRY:
                with open(MANIFEST, 'a') as f:
                    f.write(f'{p}\t{h}\t{keep}\n')
                os.remove(p)
    log(f'duplicates: {sum(len(v) - 1 for v in groups.values())} files removed, {freed / 1e9:.1f} GB')
    if DRY:
        return
    # 3. re-compress the kept files to .xz, verified
    before = after = 0
    for i, p in enumerate(sorted(kept)):
        if p.endswith('.xz'):
            continue
        h = known[p][2]
        dst = stem(p) + ('.route' if '.route' in os.path.basename(p) else '.g24sol') + '.xz'
        tmp = dst + '.tmp'
        src = subprocess.Popen(['gunzip', '-c', p] if p.endswith('.gz') else ['cat', p], stdout=subprocess.PIPE)
        with open(tmp, 'wb') as out:
            subprocess.run(['xz', '-6', '-T4', '-c'], stdin=src.stdout, stdout=out, check=True)
        if src.wait() != 0 or content_hash(tmp, xz=True) != h:
            os.remove(tmp)
            log('VERIFY FAILED, kept the original: ' + p)
            continue
        os.replace(tmp, dst)
        before += os.path.getsize(p)
        after += os.path.getsize(dst)
        os.remove(p)
        with open(MANIFEST, 'a') as f:
            f.write(f'{p}\t{h}\t{dst}\t(re-compressed)\n')
        if i % 10 == 0:
            log(f'compressed {i + 1}/{len(kept)}: {before / 1e9:.1f} GB -> {after / 1e9:.1f} GB so far')
    log(f'done: re-compressed {before / 1e9:.1f} GB -> {after / 1e9:.1f} GB')


if __name__ == '__main__':
    main()
