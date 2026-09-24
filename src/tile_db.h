#ifndef TILE_DB_H
#define TILE_DB_H

#include <stddef.h>
#include "sqlite3.h"

typedef struct {
    sqlite3 *db;

    sqlite3_stmt *get_tile_stmt;
    sqlite3_stmt *put_tile_stmt;
} TileDb;

/*
 * Opens or creates the database and initializes its schema.
 * Returns 1 on success, 0 on failure.
 */
int TileDbOpen(
    TileDb *store,
    const char *database_path,
    const char *schema_path
);

/*
 * Finalizes statements and closes the database.
 */
void TileDbClose(TileDb *store);

/*
 * Stores one encoded PNG tile.
 */
int TileDbPut(
    TileDb *store,
    const char *provider,
    const char *style,
    int zoom,
    int tile_x,
    int tile_y,
    const unsigned char *png_data,
    int png_size
);

/*
 * Retrieves one encoded PNG tile.
 *
 * On success:
 *   - returns 1
 *   - allocates *out_data
 *   - sets *out_size
 *
 * The caller must call free(*out_data).
 *
 * Returns 0 if the tile does not exist or an error occurs.
 */
int TileDbGet(
    TileDb *store,
    const char *provider,
    const char *style,
    int zoom,
    int tile_x,
    int tile_y,
    unsigned char **out_data,
    int *out_size
);

#endif