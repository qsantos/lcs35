#!/usr/bin/env python3
import sys
import pickle
import sqlite3


def _():
    for filename in sys.argv[1:]:
        with open(filename, 'rb') as f:
            d = pickle.load(f)
        for i, w in d['checkpoints'].items():
            yield i, str(w), filename


with sqlite3.connect('lcs35.db') as db:
    db.execute(
        '''CREATE TABLE IF NOT EXISTS checkpoint (i INTEGER UNIQUE, w TEXT,
        time TIMESTAMP DEFAULT CURRENT_TIMESTAMP)'''
    )

    db.executemany('INSERT OR IGNORE INTO checkpoint VALUES (?, ?, datetime(?))', _())
