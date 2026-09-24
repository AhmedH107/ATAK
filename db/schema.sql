PRAGMA journal_mode = WAL;
PRAGMA synchronous = NORMAL;
PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS tiles (
    provider TEXT NOT NULL,
    style TEXT NOT NULL,

    zoom INTEGER NOT NULL,
    tile_x INTEGER NOT NULL,
    tile_y INTEGER NOT NULL,

    image_data BLOB NOT NULL,
    last_access INTEGER NOT NULL DEFAULT (unixepoch()),

    PRIMARY KEY (
        provider,
        style,
        zoom,
        tile_x,
        tile_y
    )
) WITHOUT ROWID;

CREATE INDEX IF NOT EXISTS idx_tiles_last_access
ON tiles(last_access);