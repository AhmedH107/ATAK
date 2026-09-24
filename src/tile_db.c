#include "tile_db.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *ReadTextFile(const char *path)
{
    FILE *file = fopen(path, "rb");

    if (file == NULL) {
        fprintf(stderr, "Could not open SQL file: %s\n", path);
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }

    long size = ftell(file);

    if (size < 0) {
        fclose(file);
        return NULL;
    }

    rewind(file);

    char *text = malloc((size_t)size + 1);

    if (text == NULL) {
        fclose(file);
        return NULL;
    }

    size_t bytes_read = fread(text, 1, (size_t)size, file);
    fclose(file);

    if (bytes_read != (size_t)size) {
        free(text);
        return NULL;
    }

    text[size] = '\0';
    return text;
}

static int ExecuteSqlFile(sqlite3 *db, const char *path)
{
    char *sql = ReadTextFile(path);

    if (sql == NULL) {
        return 0;
    }

    char *error_message = NULL;

    int result = sqlite3_exec(
        db,
        sql,
        NULL,
        NULL,
        &error_message
    );

    free(sql);

    if (result != SQLITE_OK) {
        fprintf(
            stderr,
            "SQLite error while executing %s: %s\n",
            path,
            error_message != NULL
                ? error_message
                : sqlite3_errmsg(db)
        );

        sqlite3_free(error_message);
        return 0;
    }

    return 1;
}

static int ExecuteSql(sqlite3 *db, const char *sql)
{
    char *error_message = NULL;

    int result = sqlite3_exec(
        db,
        sql,
        NULL,
        NULL,
        &error_message
    );

    if (result != SQLITE_OK) {
        fprintf(
            stderr,
            "SQLite error: %s\n",
            error_message != NULL
                ? error_message
                : sqlite3_errmsg(db)
        );

        sqlite3_free(error_message);
        return 0;
    }

    return 1;
}

int TileDbOpen(
    TileDb *store,
    const char *database_path,
    const char *schema_path
)
{
    if (store == NULL ||
        database_path == NULL ||
        schema_path == NULL) {
        return 0;
    }

    memset(store, 0, sizeof(*store)); //Clears RAM

    int result = sqlite3_open_v2(
        database_path,
        &store->db,
        SQLITE_OPEN_READWRITE |
        SQLITE_OPEN_CREATE |
        SQLITE_OPEN_FULLMUTEX,
        NULL
    );

    if (result != SQLITE_OK) {
        fprintf(
            stderr,
            "Could not open database: %s\n",
            store->db != NULL
                ? sqlite3_errmsg(store->db)
                : "unknown error"
        );

        TileDbClose(store);
        return 0;
    }

    sqlite3_busy_timeout(store->db, 3000);

    if (!ExecuteSqlFile(store->db, schema_path)) {
        TileDbClose(store);
        return 0;
    }

   const char *get_sql =
    "SELECT image_data "
    "FROM tiles "
    "WHERE provider = ?1 "
    "AND style = ?2 "
    "AND zoom = ?3 "
    "AND tile_x = ?4 "
    "AND tile_y = ?5;";

result = sqlite3_prepare_v2(
    store->db,
    get_sql,
    -1,
    &store->get_tile_stmt,
    NULL
);

if (result != SQLITE_OK) {
    fprintf(
        stderr,
        "Could not prepare tile SELECT: %s\n",
        sqlite3_errmsg(store->db)
    );

    TileDbClose(store);
    return 0;
}

const char *put_sql =
    "INSERT INTO tiles ("
    "provider, "
    "style, "
    "zoom, "
    "tile_x, "
    "tile_y, "
    "image_data, "
    "last_access"
    ") "
    "VALUES (?1, ?2, ?3, ?4, ?5, ?6, unixepoch()) "
    "ON CONFLICT(provider, style, zoom, tile_x, tile_y) "
    "DO UPDATE SET "
    "image_data = excluded.image_data, "
    "last_access = unixepoch();";

result = sqlite3_prepare_v2(
    store->db,
    put_sql,
    -1,
    &store->put_tile_stmt,
    NULL
);

if (result != SQLITE_OK) {
    fprintf(
        stderr,
        "Could not prepare tile INSERT: %s\n",
        sqlite3_errmsg(store->db)
    );

    TileDbClose(store);
    return 0;
};

    return 1;
}

void TileDbClose(TileDb *store)
{
    if (store == NULL) {
        return;
    }

    if (store->get_tile_stmt != NULL) {
        sqlite3_finalize(store->get_tile_stmt);
        store->get_tile_stmt = NULL;
    }

    if (store->put_tile_stmt != NULL) {
        sqlite3_finalize(store->put_tile_stmt);
        store->put_tile_stmt = NULL;
    }

    if (store->db != NULL) {
        sqlite3_close(store->db);
        store->db = NULL;
    }
}

int TileDbPut(
    TileDb *store,
    const char *provider,
    const char *style,
    int zoom,
    int tile_x,
    int tile_y,
    const unsigned char *png_data,
    int png_size
)
{
    if (store == NULL ||
        store->put_tile_stmt == NULL ||
        provider == NULL ||
        style == NULL ||
        png_data == NULL ||
        png_size <= 0) {
        return 0;
    }

    sqlite3_stmt *statement = store->put_tile_stmt;

    sqlite3_reset(statement);
    sqlite3_clear_bindings(statement);

    sqlite3_bind_text(
        statement,
        1,
        provider,
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        statement,
        2,
        style,
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_int(statement, 3, zoom);
    sqlite3_bind_int(statement, 4, tile_x);
    sqlite3_bind_int(statement, 5, tile_y);

    sqlite3_bind_blob(
        statement,
        6,
        png_data,
        png_size,
        SQLITE_TRANSIENT
    );

    int result = sqlite3_step(statement);

    if (result != SQLITE_DONE) {
        fprintf(
            stderr,
            "Could not store tile: %s\n",
            sqlite3_errmsg(store->db)
        );

        sqlite3_reset(statement);
        return 0;
    }

    sqlite3_reset(statement);
    return 1;
}

int TileDbGet(
    TileDb *store,
    const char *provider,
    const char *style,
    int zoom,
    int tile_x,
    int tile_y,
    unsigned char **out_data,
    int *out_size
)
{
    if (out_data != NULL) {
        *out_data = NULL;
    }

    if (out_size != NULL) {
        *out_size = 0;
    }

    if (store == NULL ||
        store->get_tile_stmt == NULL ||
        provider == NULL ||
        style == NULL ||
        out_data == NULL ||
        out_size == NULL) {
        return 0;
    }

    sqlite3_stmt *statement = store->get_tile_stmt;

    sqlite3_reset(statement);
    sqlite3_clear_bindings(statement);

    sqlite3_bind_text(
        statement,
        1,
        provider,
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        statement,
        2,
        style,
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_int(statement, 3, zoom);
    sqlite3_bind_int(statement, 4, tile_x);
    sqlite3_bind_int(statement, 5, tile_y);

    int result = sqlite3_step(statement);

    if (result == SQLITE_DONE) {
        sqlite3_reset(statement);
        return 0; /* We should get SQLITE_ROW after _step(), if SQLITE_DONE, then we found nothing*/
    }

    if (result != SQLITE_ROW) {
        fprintf(
            stderr,
            "Could not retrieve tile: %s\n",
            sqlite3_errmsg(store->db)
        );

        sqlite3_reset(statement);
        return 0;
    }

    const void *database_blob =
        sqlite3_column_blob(statement, 0); // from SELECT we return 0:image_data

    int database_blob_size =
        sqlite3_column_bytes(statement, 0);

    if (database_blob == NULL || database_blob_size <= 0) {
        sqlite3_reset(statement);
        return 0;
    }

    unsigned char *copy =
        malloc((size_t)database_blob_size);

    if (copy == NULL) {
        fprintf(stderr, "Out of memory while loading tile\n");
        sqlite3_reset(statement);
        return 0;
    }

    /*
     * sqlite3_column_blob() is only valid while the current result
     * row remains active, so copy it before resetting the statement.
     */
    memcpy(copy, database_blob, (size_t)database_blob_size);

    *out_data = copy;
    *out_size = database_blob_size;

    sqlite3_reset(statement);
    return 1;
}