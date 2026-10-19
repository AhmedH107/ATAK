#include "tile_manager.h"
#include "geo.h"
#include "tile_network.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

// Database give png raylab wants textu
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

static void RequestTile(
    TileDb *tile_db,
    int zoom,
    int requested_x,
    int y,
    const char *api_key)
{
    int tiles_at_zoom =
        1 << zoom;

    /*
     * X wraps around the Earth.
     * Y does not.
     */
    if (y < 0 ||
        y >= tiles_at_zoom)
    {
        return;
    }

    int x =
        NormalizeTileX(
            requested_x,
            zoom);

    /*
     * Already loaded as a GPU texture.
     */
    if (FindCachedTile(
            zoom,
            x,
            y) != NULL)
    {
        return;
    }

    /*
     * Network request already exists.
     */
    if (TileNetworkIsPending(
            zoom,
            x,
            y))
    {
        return;
    }

    /*
     * Try persistent SQLite cache.
     */
    MapTile *database_tile =
        LoadTileFromDatabase(
            tile_db,
            zoom,
            x,
            y);

    if (database_tile != NULL)
    {
        return;
    }

    /*
     * SQLite miss:
     * request asynchronously from network.
     */
    TileNetworkRequest(
        zoom,
        x,
        y,
        api_key);
}

static void ProcessCompletedDownloads(
    TileDb *tile_db,
    int max_to_process)
{
    for (int processed = 0;
         processed < max_to_process;
         ++processed)
    {
        TileDownloadResult result;

        if (!TileNetworkTakeCompleted(
                &result))
        {
            break;
        }

        if (result.data == NULL ||
            result.size == 0)
        {
            free(result.data);
            continue;
        }

        /*
         * Save original PNG bytes into
         * persistent SQLite cache.
         */
        TileDbPut(
            tile_db,
            "maptiler",
            "streets-v2",
            result.zoom,
            result.x,
            result.y,
            result.data,
            (int)result.size);

        /*
         * Decode PNG and upload it to GPU.
         */
        Texture2D texture =
            CreateTextureFromPng(
                result.data,
                (int)result.size);

        if (texture.id != 0)
        {
            AddTileToCache(
                result.zoom,
                result.x,
                result.y,
                texture);
        }

        /*
         * TileNetwork transferred ownership
         * of this memory to us.
         */
        free(result.data);
    }
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

        tile_cache[i] =
            tile_cache[tile_count - 1];

        tile_count--;
    }
}

static void RequestVisibleTiles(
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
            RequestTile(
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

    RequestVisibleTiles(
        camera,
        zoom,
        tile_db,
        api_key);

    TileNetworkUpdate();

    ProcessCompletedDownloads(
        tile_db,
        2);
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

    TileNetworkShutdown();
}