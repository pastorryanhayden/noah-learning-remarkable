#!/usr/bin/env python3
"""Create an integrity-checked SQLite online backup without copying a live WAL."""
import argparse
import os
from pathlib import Path
import sqlite3
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('source', type=Path)
parser.add_argument('destination', type=Path)
args = parser.parse_args()
if not args.source.is_file():
    parser.error('source database does not exist')
args.destination.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
if args.destination.exists():
    parser.error('destination already exists')
os.umask(0o077)
fd, temporary = tempfile.mkstemp(prefix='.sqlite-backup-', dir=args.destination.parent)
os.close(fd)
try:
    with sqlite3.connect(args.source.resolve().as_uri() + '?mode=ro', uri=True, timeout=30) as source:
        with sqlite3.connect(temporary) as destination:
            source.backup(destination)
            if destination.execute('PRAGMA integrity_check').fetchall() != [('ok',)]:
                raise RuntimeError('backup integrity check failed')
    with open(temporary, 'rb') as stream:
        os.fsync(stream.fileno())
    # Refuse replacement even if another process created the target meanwhile.
    os.link(temporary, args.destination)
finally:
    os.unlink(temporary)
