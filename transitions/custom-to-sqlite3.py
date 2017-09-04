#!/usr/bin/env python3
import sys
import sqlite3


def _():
    for filename in sys.argv[1:]:
        with open(filename) as f:
            t, i, c, n, w, *n_validations = f.readlines()
        yield i, w, filename


with sqlite3.connect('lcs35.db') as db:
    db.execute(
        '''CREATE TABLE IF NOT EXISTS checkpoint (i INTEGER UNIQUE, w TEXT,
        time TIMESTAMP DEFAULT CURRENT_TIMESTAMP)'''
    )

    db.executemany('INSERT OR IGNORE INTO checkpoint VALUES (?, ?, datetime(?))', _())
