CREATE TABLE tmp (
    i INTEGER UNIQUE,
    w TEXT,
    first_computed TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    last_computed TIMESTAMP DEFAULT CURRENT_TIMESTAMP
);
INSERT INTO tmp (i, w, first_computed, last_computed) SELECT i, w, time, time FROM checkpoint;
DROP TABLE checkpoint;
ALTER TABLE tmp RENAME TO checkpoint;
