#include "tile_manager.h"
#include "geo.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define TILE_SIZE 256
#define MAX_LOADED_TILES 256
#define TILE_LOAD_PADDING 1
#define TILE_KEEP_PADDING 4

typedef struct
{
    int zoom;
    int x;
    int y;
    Texture2D texture;
} MapTile;

/*
 * Private tile state.
 *
 * Only tile_manager.c can see these.
 */
static MapTile tile_cache[MAX_LOADED_TILES];
static int tile_count = 0;

/* --------------------------------------------------------- */
/* Temporary networking helpers                              */
/* --------------------------------------------------------- */

static int DownloadFile(
    const char *url,
    const char *output_path)
{
    char command[2048];

    int written = snprintf(
        command,
        sizeof(command),
        "curl.exe --fail --silent --show-error --location "
        "--output \"%s\" \"%s\"",
        output_path,
        url);

    if (written < 0 ||
        written >= (int)sizeof(command))
    {
        fprintf(
            stderr,
            "Download command is too long\n");

        return 0;
    }

    int result = system(command);

    if (result != 0)
    {
        fprintf(
            stderr,
            "Download failed with code %d\n",
            result);

        return 0;
    }

    return 1;
}

static unsigned char *ReadBinaryFile(
    const char *path,
    int *out_size)
{
    if (out_size == NULL)
    {
        return NULL;
    }

    *out_size = 0;

    FILE *file = fopen(path, "rb");

    if (file == NULL)
    {
        fprintf(
            stderr,
            "Could not open binary file: %s\n",
            path);

        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0)
    {
        fclose(file);
        return NULL;
    }

    long file_size = ftell(file);

    if (file_size <= 0)
    {
        fclose(file);
        return NULL;
    }

    rewind(file);

    unsigned char *data =
        malloc((size_t)file_size);

    if (data == NULL)
    {
        fclose(file);
        return NULL;
    }

    size_t bytes_read = fread(
        data,
        1,
        (size_t)file_size,
        file);

    fclose(file);

    if (bytes_read != (size_t)file_size)
    {
        free(data);
        return NULL;
    }

    *out_size = (int)file_size;

    return data;
}

static int DownloadTile(
    int zoom,
    int x,
    int y,
    const char *api_key,
    unsigned char **out_data,
    int *out_size)
{
    if (api_key == NULL ||
        out_data == NULL ||
        out_size == NULL)
    {
        return 0;
    }

    *out_data = NULL;
    *out_size = 0;

    char url[1024];
    char temp_path[256];

    snprintf(
        url,
        sizeof(url),
        "https://api.maptiler.com/maps/streets-v2/256/"
        "%d/%d/%d.png?key=%s",
        zoom,
        x,
        y,
        api_key);

    snprintf(
        temp_path,
        sizeof(temp_path),
        "data/.tile_%d_%d_%d.tmp",
        zoom,
        x,
        y);

    if (!DownloadFile(url, temp_path))
    {
        fprintf(
            stderr,
            "Download failed for tile %d/%d/%d\n",
            zoom,
            x,
            y);

        return 0;
    }

    unsigned char *data =
        ReadBinaryFile(
            temp_path,
            out_size);

    remove(temp_path);

    if (data == NULL)
    {
        fprintf(
            stderr,
            "Could not read downloaded tile %d/%d/%d\n",
            zoom,
            x,
            y);

        return 0;
    }

    *out_data = data;

    return 1;
}

// Database give png raylab wants texture
static Texture2D CreateTextureFromPng(
    const unsigned char *png_data,
    int png_size)
{
    Texture2D texture = {0};

    if (png_data == NULL ||
        png_size <= 0)
    {
        return texture;
    }

    Image image =
        LoadImageFromMemory(
            ".png",
            png_data,
            png_size);

    if (!IsImageValid(image))
    {
        fprintf(
            stderr,
            "Could not decode PNG tile\n");

        return texture;
    }

    texture =
        LoadTextureFromImage(image);

    UnloadImage(image);

    return texture;
}

static MapTile *FindCachedTile(
    int zoom,
    int x,
    int y)
{
    for (int i = 0;
         i < tile_count;
         ++i)
    {
        if (tile_cache[i].zoom == zoom &&
            tile_cache[i].x == x &&
            tile_cache[i].y == y)
        {
            return &tile_cache[i];
        }
    }

    return NULL;
}

static MapTile *AddTileToCache(
    int zoom,
    int x,
    int y,
    Texture2D texture)
{
    if (texture.id == 0)
    {
        return NULL;
    }

    if (tile_count >= MAX_LOADED_TILES)
    {
        fprintf(
            stderr,
            "Tile cache is full\n");

        UnloadTexture(texture);

        return NULL;
    }

    MapTile *tile =
        &tile_cache[tile_count++];

    tile->zoom = zoom;
    tile->x = x;
    tile->y = y;
    tile->texture = texture;

    return tile;
}

/* --------------------------------------------------------- */
/* SQLite                                                    */
/* --------------------------------------------------------- */

static MapTile *LoadTileFromDatabase(
    TileDb *tile_db,
    int zoom,
    int x,
    int y)
{
    unsigned char *png_data = NULL;
    int png_size = 0;

    int found = TileDbGet(
        tile_db,
        "maptiler",
        "streets-v2",
        zoom,
        x,
        y,
        &png_data,
        &png_size);

    if (!found)
    {
        return NULL;
    }

    Texture2D texture =
        CreateTextureFromPng(
            png_data,
            png_size);

    free(png_data);

    if (texture.id == 0)
    {
        fprintf(
            stderr,
            "Could not create texture for cached tile "
            "%d/%d/%d\n",
            zoom,
            x,
            y);

        return NULL;
    }

    return AddTileToCache(
        zoom,
        x,
        y,
        texture);
}

/* --------------------------------------------------------- */
/* Visible tiles                                             */
/* --------------------------------------------------------- */

static void GetVisibleTileRange(
    Camera2D camera,
    int padding,
    int *min_x,
    int *max_x,
    int *min_y,
    int *max_y)
{
    Vector2 corner_a =
        GetScreenToWorld2D(
            (Vector2){0.0f, 0.0f},
            camera);

    Vector2 corner_b =
        GetScreenToWorld2D(
            (Vector2){
                (float)GetScreenWidth(),
                (float)GetScreenHeight()},
            camera);

    float left =
        fminf(corner_a.x, corner_b.x);

    float right =
        fmaxf(corner_a.x, corner_b.x);

    float top =
        fminf(corner_a.y, corner_b.y);

    float bottom =
        fmaxf(corner_a.y, corner_b.y);

    *min_x =
        (int)floorf(left / TILE_SIZE) - padding;

    *max_x =
        (int)floorf(right / TILE_SIZE) + padding;

    *min_y =
        (int)floorf(top / TILE_SIZE) - padding;

    *max_y =
        (int)floorf(bottom / TILE_SIZE) + padding;
}

/* --------------------------------------------------------- */
/* Load one tile                                             */
/* --------------------------------------------------------- */

static MapTile *LoadTile(
    TileDb *tile_db,
    int zoom,
    int requested_x,
    int y,
    const char *api_key)
{
    int tiles_per_axis =
        1 << zoom;

    /*
     * Vertical tile coordinates do not wrap.
     */
    if (y < 0 ||
        y >= tiles_per_axis)
    {
        return NULL;
    }

    /*
     * Horizontal tile coordinates wrap.
     */
    int x =
        NormalizeTileX(
            requested_x,
            zoom);

    // already loaded into GPU memory?
    MapTile *cached =
        FindCachedTile(
            zoom,
            x,
            y);

    if (cached != NULL)
    {
        return cached;
    }

    if (tile_count >= MAX_LOADED_TILES)
    {
        return NULL;
    }

    /*
     * Level 2:
     * stored in SQLite?
     */
    MapTile *database_tile =
        LoadTileFromDatabase(
            tile_db,
            zoom,
            x,
            y);

    if (database_tile != NULL)
    {
        return database_tile;
    }

    /*
     * Level 3:
     * download from provider.
     */
    unsigned char *downloaded_data = NULL;
    int downloaded_size = 0;

    if (!DownloadTile(
            zoom,
            x,
            y,
            api_key,
            &downloaded_data,
            &downloaded_size))
    {
        return NULL;
    }

    /*
     * Save to persistent SQLite cache.
     *
     * Failure here is not fatal.
     * We can still display the downloaded tile.
     */
    if (!TileDbPut(
            tile_db,
            "maptiler",
            "streets-v2",
            zoom,
            x,
            y,
            downloaded_data,
            downloaded_size))
    {
        fprintf(
            stderr,
            "Warning: could not cache tile "
            "%d/%d/%d\n",
            zoom,
            x,
            y);
    }

    Texture2D texture =
        CreateTextureFromPng(
            downloaded_data,
            downloaded_size);

    free(downloaded_data);

    if (texture.id == 0)
    {
        return NULL;
    }

    return AddTileToCache(
        zoom,
        x,
        y,
        texture);
}

/* --------------------------------------------------------- */
/* Loading/unloading                                         */
/* --------------------------------------------------------- */

static void UnloadDistantTiles(
    Camera2D camera,
    int zoom)
{
    int min_x = 0;
    int max_x = 0;
    int min_y = 0;
    int max_y = 0;

    GetVisibleTileRange(
        camera,
        TILE_KEEP_PADDING,
        &min_x,
        &max_x,
        &min_y,
        &max_y);

    int i = 0;

    while (i < tile_count)
    {
        MapTile *tile =
            &tile_cache[i];

        int keep =
            tile->zoom == zoom &&
            tile->x >= min_x &&
            tile->x <= max_x &&
            tile->y >= min_y &&
            tile->y <= max_y;

        if (keep)
        {
            i++;
            continue;
        }

        UnloadTexture(
            tile->texture);

        /*
         * Replace the deleted item with the
         * last item in the array.
         */
        tile_cache[i] =
            tile_cache[tile_count - 1];

        tile_count--;
    }
}

static void EnsureVisibleTiles(
    Camera2D camera,
    int zoom,
    TileDb *tile_db,
    const char *api_key)
{
    int min_x = 0;
    int max_x = 0;
    int min_y = 0;
    int max_y = 0;

    GetVisibleTileRange(
        camera,
        TILE_LOAD_PADDING,
        &min_x,
        &max_x,
        &min_y,
        &max_y);

    for (int y = min_y;
         y <= max_y;
         ++y)
    {
        for (int x = min_x;
             x <= max_x;
             ++x)
        {
            LoadTile(
                tile_db,
                zoom,
                x,
                y,
                api_key);
        }
    }
}

/* --------------------------------------------------------- */
/* Public functions                                          */
/* --------------------------------------------------------- */

void TileManagerUpdate(
    Camera2D camera,
    int zoom,
    TileDb *tile_db,
    const char *api_key)
{
    UnloadDistantTiles(
        camera,
        zoom);

    EnsureVisibleTiles(
        camera,
        zoom,
        tile_db,
        api_key);
}

void TileManagerDraw(int zoom)
{
    for (int i = 0;
         i < tile_count;
         ++i)
    {
        MapTile *tile =
            &tile_cache[i];

        if (tile->zoom != zoom ||
            tile->texture.id == 0)
        {
            continue;
        }

        DrawTexture(
            tile->texture,
            tile->x * TILE_SIZE,
            tile->y * TILE_SIZE,
            WHITE);
    }
}

int TileManagerGetLoadedCount(void)
{
    return tile_count;
}

void TileManagerShutdown(void)
{
    for (int i = 0;
         i < tile_count;
         ++i)
    {
        if (tile_cache[i].texture.id != 0)
        {
            UnloadTexture(
                tile_cache[i].texture);
        }
    }

    tile_count = 0;
}