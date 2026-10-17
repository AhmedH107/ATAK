#include "tile_manager.h"
#include "geo.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32

#define CloseWindow CloseWindowWin32
#define Rectangle RectangleWin32
#define ShowCursor ShowCursorWin32

#define DrawTextA DrawTextAWin32
#define DrawTextW DrawTextWWin32
#define DrawTextExA DrawTextExAWin32
#define DrawTextExW DrawTextExWWin32

#endif

#include <curl/curl.h>

// Horrible way to stop raylib and C:/msys64/ucrt64/bin/gcc.exe from clashing idk how to fix better
#ifdef _WIN32

#undef CloseWindow
#undef Rectangle
#undef ShowCursor

#undef LoadImage
#undef DrawText
#undef DrawTextA
#undef DrawTextW
#undef DrawTextExA
#undef DrawTextExW

#endif
//

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

typedef enum
{
    TILE_REQUEST_NONE,
    TILE_REQUEST_DOWNLOADING,
    TILE_REQUEST_READY,
    TILE_REQUEST_FAILED
} TileRequestState;

typedef struct
{
    int in_use;

    int zoom;
    int x;
    int y;

    TileRequestState state;

    unsigned char *data;
    size_t size;

    CURL *curl;

    char url[1024];
} TileRequest;

#define MAX_PENDING_DOWNLOADS 32

static TileRequest pending_downloads[MAX_PENDING_DOWNLOADS];

static CURLM *curl_multi = NULL;

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

static TileRequest *FindPendingDownload(
    int zoom,
    int x,
    int y);

static TileRequest *AllocateRequest(void);

static void QueueTileDownload(
    int zoom,
    int x,
    int y,
    const char *api_key);

static size_t CurlWriteCallback(
    char *incoming,
    size_t size,
    size_t nmemb,
    void *user_data);

static TileRequest *FindPendingDownload(
    int zoom,
    int x,
    int y)
{
    for (int i = 0;
         i < MAX_PENDING_DOWNLOADS;
         ++i)
    {
        TileRequest *request =
            &pending_downloads[i];

        if (!request->in_use)
        {
            continue;
        }

        if (request->zoom == zoom &&
            request->x == x &&
            request->y == y)
        {
            return request;
        }
    }

    return NULL;
}

static TileRequest *AllocateRequest(void)
{
    for (int i = 0;
         i < MAX_PENDING_DOWNLOADS;
         ++i)
    {
        TileRequest *request =
            &pending_downloads[i];

        if (request->in_use)
        {
            continue;
        }

        memset(
            request,
            0,
            sizeof(*request));

        request->in_use = 1;

        return request;
    }

    return NULL;
}

static void RequestTile(
    TileDb *tile_db,
    int zoom,
    int requested_x,
    int y,
    const char *api_key)
{
    int x =
        NormalizeTileX(
            requested_x,
            zoom);

    // In CPU Cache
    if (FindCachedTile(zoom, x, y) != NULL)
    {
        return;
    }

    // Already being downloaded
    if (FindPendingDownload(zoom, x, y) != NULL)
    {
        return;
    }

    // Try sql cache
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

    // Start the download
    QueueTileDownload(
        zoom,
        x,
        y,
        api_key);
}

static void QueueTileDownload(
    int zoom,
    int x,
    int y,
    const char *api_key)
{
    TileRequest *request =
        AllocateRequest();

    if (request == NULL)
    {
        return;
    }

    request->zoom = zoom;
    request->x = x;
    request->y = y;

    request->state =
        TILE_REQUEST_DOWNLOADING;

    request->data = NULL;
    request->size = 0;

    int written = snprintf(
        request->url,
        sizeof(request->url),
        "https://api.maptiler.com/maps/streets-v2/256/"
        "%d/%d/%d.png?key=%s",
        zoom,
        x,
        y,
        api_key);

    if (written < 0 ||
        written >= (int)sizeof(request->url))
    {
        request->state =
            TILE_REQUEST_FAILED;

        request->in_use = 0;

        return;
    }

    request->curl =
        curl_easy_init();

    if (request->curl == NULL)
    {
        request->state =
            TILE_REQUEST_FAILED;

        request->in_use = 0;

        return;
    }

    CURLcode result;

    result = curl_easy_setopt(
        request->curl,
        CURLOPT_URL,
        request->url);

    if (result != CURLE_OK)
    {
        goto fail;
    }

    result = curl_easy_setopt(
        request->curl,
        CURLOPT_WRITEFUNCTION,
        CurlWriteCallback);

    if (result != CURLE_OK)
    {
        goto fail;
    }

    result = curl_easy_setopt(
        request->curl,
        CURLOPT_WRITEDATA,
        request);

    if (result != CURLE_OK)
    {
        goto fail;
    }

    /*
     * Store our TileRequest pointer inside the CURL handle.
     *
     * Later, when curl_multi_info_read() tells us which
     * CURL handle finished, we can recover the request.
     */
    result = curl_easy_setopt(
        request->curl,
        CURLOPT_PRIVATE,
        request);

    if (result != CURLE_OK)
    {
        goto fail;
    }

    /*
     * Follow HTTP redirects if the provider sends one.
     */
    result = curl_easy_setopt(
        request->curl,
        CURLOPT_FOLLOWLOCATION,
        1L);

    if (result != CURLE_OK)
    {
        goto fail;
    }

    /*
     * Treat HTTP 4xx / 5xx responses as errors.
     */
    result = curl_easy_setopt(
        request->curl,
        CURLOPT_FAILONERROR,
        1L);

    if (result != CURLE_OK)
    {
        goto fail;
    }

    CURLMcode multi_result =
        curl_multi_add_handle(
            curl_multi,
            request->curl);

    if (multi_result != CURLM_OK)
    {
        goto fail;
    }

    return;

fail:

    if (request->curl != NULL)
    {
        curl_easy_cleanup(
            request->curl);

        request->curl = NULL;
    }

    free(request->data);

    request->data = NULL;
    request->size = 0;

    request->state =
        TILE_REQUEST_FAILED;

    request->in_use = 0;
}

static size_t CurlWriteCallback(
    char *incoming,
    size_t size,
    size_t count,
    void *user_data)
{
    TileRequest *request =
        user_data;

    size_t incoming_size =
        size * count;

    unsigned char *new_data =
        realloc(
            request->data,
            request->size +
                incoming_size);

    if (new_data == NULL)
    {
        return 0;
    }

    request->data =
        new_data;

    memcpy(
        request->data +
            request->size,
        incoming,
        incoming_size);

    request->size += incoming_size;

    return incoming_size;
}

static void UpdateDownloads(void)
{
    int running = 0;

    CURLMcode perform_result =
        curl_multi_perform(
            curl_multi,
            &running);

    if (perform_result != CURLM_OK)
    {
        fprintf(
            stderr,
            "curl_multi_perform failed: %s\n",
            curl_multi_strerror(perform_result));

        return;
    }

    CURLMsg *message;
    int messages_left = 0;

    while ((message =
                curl_multi_info_read(
                    curl_multi,
                    &messages_left)) != NULL)
    {
        if (message->msg != CURLMSG_DONE)
        {
            continue;
        }

        CURL *easy =
            message->easy_handle;

        TileRequest *request = NULL;

        curl_easy_getinfo(
            easy,
            CURLINFO_PRIVATE,
            &request);

        long http_code = 0;

        curl_easy_getinfo(
            easy,
            CURLINFO_RESPONSE_CODE,
            &http_code);

        CURLcode transfer_result =
            message->data.result;

        curl_multi_remove_handle(
            curl_multi,
            easy);

        curl_easy_cleanup(easy);

        if (request == NULL)
        {
            continue;
        }

        request->curl = NULL;

        if (transfer_result == CURLE_OK &&
            http_code >= 200 &&
            http_code < 300 &&
            request->data != NULL &&
            request->size > 0)
        {

            request->state =
                TILE_REQUEST_READY;
        }
        else
        {
            fprintf(
                stderr,
                "Tile download failed %d/%d/%d: "
                "curl=%s http=%ld\n",
                request->zoom,
                request->x,
                request->y,
                curl_easy_strerror(
                    transfer_result),
                http_code);

            request->state =
                TILE_REQUEST_FAILED;
        }
    }
}

static void ProcessCompletedDownloads(
    TileDb *tile_db,
    int max_to_process)
{
    int processed = 0;

    for (int i = 0;
         i < MAX_PENDING_DOWNLOADS;
         ++i)
    {
        TileRequest *request =
            &pending_downloads[i];

        if (!request->in_use)
        {
            continue;
        }

        /*
         * Failed request:
         * clean the slot so it can be reused later.
         */
        if (request->state ==
            TILE_REQUEST_FAILED)
        {
            free(request->data);

            request->data = NULL;
            request->size = 0;
            request->curl = NULL;

            request->state =
                TILE_REQUEST_NONE;

            request->in_use = 0;

            continue;
        }

        /*
         * Not finished yet.
         */
        if (request->state !=
            TILE_REQUEST_READY)
        {
            continue;
        }

        /*
         * Limit PNG decoding / GPU uploads
         * per frame.
         */
        if (processed >= max_to_process)
        {
            break;
        }

        if (request->data != NULL &&
            request->size > 0)
        {
            /*
             * Save downloaded PNG into SQLite.
             */
            TileDbPut(
                tile_db,
                "maptiler",
                "streets-v2",
                request->zoom,
                request->x,
                request->y,
                request->data,
                (int)request->size);

            /*
             * Decode PNG and upload to GPU.
             */
            Texture2D texture =
                CreateTextureFromPng(
                    request->data,
                    (int)request->size);

            if (texture.id != 0)
            {
                AddTileToCache(
                    request->zoom,
                    request->x,
                    request->y,
                    texture);
            }
        }

        /*
         * PNG bytes are no longer needed.
         */
        free(request->data);

        request->data = NULL;
        request->size = 0;
        request->curl = NULL;

        request->state =
            TILE_REQUEST_NONE;

        request->in_use = 0;

        processed++;
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
static int EnsureCurlInitialized(void)
{
    if (curl_multi != NULL)
    {
        return 1;
    }

    CURLcode global_result =
        curl_global_init(CURL_GLOBAL_DEFAULT);

    if (global_result != CURLE_OK)
    {
        fprintf(
            stderr,
            "curl_global_init failed: %s\n",
            curl_easy_strerror(global_result));

        return 0;
    }

    curl_multi =
        curl_multi_init();

    if (curl_multi == NULL)
    {
        fprintf(
            stderr,
            "curl_multi_init failed\n");

        curl_global_cleanup();

        return 0;
    }

    curl_multi_setopt(
        curl_multi,
        CURLMOPT_MAX_TOTAL_CONNECTIONS,
        8L);

    return 1;
}

void TileManagerUpdate(
    Camera2D camera,
    int zoom,
    TileDb *tile_db,
    const char *api_key)
{
    if (!EnsureCurlInitialized())
    {
        return;
    }

    UnloadDistantTiles(
        camera,
        zoom);

    RequestVisibleTiles(
        camera,
        zoom,
        tile_db,
        api_key);

    UpdateDownloads();

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

    for (int i = 0;
         i < MAX_PENDING_DOWNLOADS;
         ++i)
    {
        TileRequest *request =
            &pending_downloads[i];

        if (request->curl != NULL)
        {
            if (curl_multi != NULL)
            {
                curl_multi_remove_handle(
                    curl_multi,
                    request->curl);
            }

            curl_easy_cleanup(
                request->curl);

            request->curl = NULL;
        }

        free(request->data);

        request->data = NULL;
        request->size = 0;
        request->in_use = 0;
        request->state = TILE_REQUEST_NONE;
    }

    if (curl_multi != NULL)
    {
        curl_multi_cleanup(curl_multi);
        curl_multi = NULL;

        curl_global_cleanup();
    }
}