#include "raylib.h"
#include "tile_db.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DELETE_RADIUS 18.0f
#define MAX_MARKERS 1024
#define TILE_SIZE 256
#define MAX_LOADED_TILES 256
#define TILE_LOAD_PADDING 1
#define TILE_KEEP_PADDING 4

typedef struct
{
    double lat;
    double lon;
    int type;
    char label[64];
} Marker;

typedef struct
{
    int zoom;
    int x;
    int y;
    Texture2D texture;
} MapTile;

static Marker markers[MAX_MARKERS];
static int marker_count = 0;
static int chosen_type = 1;

static MapTile tile_cache[MAX_LOADED_TILES];
static int tile_count = 0;

static const Color marker_colors[] = {
    GRAY,
    RED,
    BLUE,
    ORANGE};

static int DownloadFile(const char *url, const char *output_path)
{
    char command[2048];

    int written = snprintf(
        command,
        sizeof(command),
        "curl.exe --fail --silent --show-error --location "
        "--output \"%s\" \"%s\"",
        output_path,
        url);

    if (written < 0 || written >= (int)sizeof(command))
    {
        fprintf(stderr, "Download command is too long\n");
        return 0;
    }

    int result = system(command);

    if (result != 0)
    {
        fprintf(stderr, "Download failed with code %d\n", result);
        return 0;
    }

    return 1;
}

static Vector2 LatLonToWorldPixel(double lat, double lon, int zoom)
{
    const double world_size = TILE_SIZE * pow(2.0, zoom);
    const double sin_lat = sin(lat * M_PI / 180.0);

    double x = (lon + 180.0) / 360.0 * world_size;
    double y = (0.5 - log((1.0 + sin_lat) / (1.0 - sin_lat)) /
                          (4.0 * M_PI)) *
               world_size;

    return (Vector2){(float)x, (float)y};
}

static void WorldPixelToLatLon(
    Vector2 pixel,
    int zoom,
    double *lat,
    double *lon)
{
    const double world_size = TILE_SIZE * pow(2.0, zoom);

    *lon = pixel.x / world_size * 360.0 - 180.0;

    double n = M_PI - 2.0 * M_PI * pixel.y / world_size;
    *lat = 180.0 / M_PI * atan(sinh(n));
}

// Database sends a PNG, Raylib want a Texture2D
static Texture2D CreateTextureFromPng(
    const unsigned char *png_data,
    int png_size)
{
    Texture2D texture = {0};

    if (png_data == NULL || png_size <= 0)
    {
        return texture;
    }

    Image image = LoadImageFromMemory(
        ".png",
        png_data,
        png_size);

    if (!IsImageValid(image))
    {
        fprintf(stderr, "Could not decode PNG tile\n");
        return texture;
    }

    texture = LoadTextureFromImage(image);

    UnloadImage(image);

    return texture;
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
        fprintf(stderr, "Tile cache is full\n");
        UnloadTexture(texture);
        return NULL;
    }

    MapTile *tile = &tile_cache[tile_count++];

    tile->zoom = zoom;
    tile->x = x;
    tile->y = y;
    tile->texture = texture;

    return tile;
}

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

    /*
     * TileDbGet() allocates this buffer for us,
     * so we own it and must free it.
     */
    free(png_data);

    if (texture.id == 0)
    {
        fprintf(
            stderr,
            "Could not create texture for cached tile %d/%d/%d\n",
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

static void SaveMarkers(const char *path)
{
    FILE *file = fopen(path, "w");
    if (file == NULL)
    {
        fprintf(stderr, "Could not save markers to %s\n", path);
        return;
    }

    for (int i = 0; i < marker_count; ++i)
    {
        fprintf(
            file,
            "%.8f %.8f %d\n",
            markers[i].lat,
            markers[i].lon,
            markers[i].type);
    }

    fclose(file);
}

static void LoadMarkers(const char *path)
{
    FILE *file = fopen(path, "r");
    if (file == NULL)
    {
        return;
    }

    marker_count = 0;

    while (marker_count < MAX_MARKERS)
    {
        double lat = 0.0;
        double lon = 0.0;
        int type = 1;

        if (fscanf(file, "%lf %lf %d", &lat, &lon, &type) != 3)
        {
            break;
        }

        markers[marker_count].lat = lat;
        markers[marker_count].lon = lon;
        markers[marker_count].type = type;
        marker_count++;
    }

    fclose(file);
}

static int FindMarkerNearMouse(Vector2 mouse, Camera2D camera, int zoom)
{
    int best_index = -1;
    float best_distance_squared = DELETE_RADIUS * DELETE_RADIUS;

    for (int i = 0; i < marker_count; ++i)
    {
        Vector2 world_pos = LatLonToWorldPixel(
            markers[i].lat,
            markers[i].lon,
            zoom);
        Vector2 screen_pos = GetWorldToScreen2D(world_pos, camera);

        float dx = mouse.x - screen_pos.x;
        float dy = mouse.y - screen_pos.y;
        float distance_squared = dx * dx + dy * dy;

        if (distance_squared <= best_distance_squared)
        {
            best_distance_squared = distance_squared;
            best_index = i;
        }
    }

    return best_index;
}

static void DeleteMarker(int index)
{
    if (index < 0 || index >= marker_count)
    {
        return;
    }

    for (int i = index; i < marker_count - 1; ++i)
    {
        markers[i] = markers[i + 1];
    }

    marker_count--;
}

static void GetVisibleTileRange(
    Camera2D camera,
    int padding,
    int *min_x,
    int *max_x,
    int *min_y,
    int *max_y)
{
    Vector2 corner_a = GetScreenToWorld2D((Vector2){0.0f, 0.0f}, camera);
    Vector2 corner_b = GetScreenToWorld2D(
        (Vector2){(float)GetScreenWidth(), (float)GetScreenHeight()},
        camera);

    float left = fminf(corner_a.x, corner_b.x);
    float right = fmaxf(corner_a.x, corner_b.x);
    float top = fminf(corner_a.y, corner_b.y);
    float bottom = fmaxf(corner_a.y, corner_b.y);

    *min_x = (int)floorf(left / TILE_SIZE) - padding;
    *max_x = (int)floorf(right / TILE_SIZE) + padding;
    *min_y = (int)floorf(top / TILE_SIZE) - padding;
    *max_y = (int)floorf(bottom / TILE_SIZE) + padding;
}

static int NormalizeTileX(int x, int zoom)
{
    int tile_count_at_zoom = 1 << zoom;
    x %= tile_count_at_zoom;

    if (x < 0)
    {
        x += tile_count_at_zoom;
    }

    return x;
}

static MapTile *FindCachedTile(int zoom, int x, int y)
{
    for (int i = 0; i < tile_count; ++i)
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

    /*
     * We only needed the temporary file because
     * DownloadFile currently uses curl.exe.
     */
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

static MapTile *LoadTile(
    TileDb *tile_db,
    int zoom,
    int requested_x,
    int y,
    const char *api_key)
{
    int tiles_per_axis = 1 << zoom;

    /*
     * Y does not wrap around the earth.
     */
    if (y < 0 || y >= tiles_per_axis)
    {
        return NULL;
    }

    /*
     * X does wrap around.
     */
    int x = NormalizeTileX(
        requested_x,
        zoom);

    /*
     * LEVEL 1 CACHE:
     *
     * Is the tile already loaded as a GPU texture?
     */
    MapTile *cached =
        FindCachedTile(
            zoom,
            x,
            y);

    if (cached != NULL)
    {
        return cached;
    }

    /*
     * Make sure there is somewhere to put another texture.
     */
    if (tile_count >= MAX_LOADED_TILES)
    {
        fprintf(stderr, "GPU tile cache is full\n");
        return NULL;
    }

    /*
     * LEVEL 2 CACHE:
     *
     * Is the compressed PNG stored in SQLite?
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
     * LEVEL 3:
     *
     * We don't have the tile locally.
     * Fetch it from the map provider.
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
        fprintf(
            stderr,
            "Could not download tile %d/%d/%d\n",
            zoom,
            x,
            y);

        return NULL;
    }

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
            "Warning: could not cache tile %d/%d/%d\n",
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
        fprintf(
            stderr,
            "Could not create texture from downloaded tile %d/%d/%d\n",
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

    return NULL;
}

static void UnloadDistantTiles(Camera2D camera, int zoom)
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
        MapTile *tile = &tile_cache[i];

        int keep = tile->zoom == zoom &&
                   tile->x >= min_x && tile->x <= max_x &&
                   tile->y >= min_y && tile->y <= max_y;

        if (keep)
        {
            i++;
            continue;
        }

        UnloadTexture(tile->texture);
        tile_cache[i] = tile_cache[tile_count - 1];
        tile_count--;
    }
}

static void EnsureVisibleTiles(
    Camera2D camera,
    int zoom,
    const char *api_key,
    TileDb *tile_db)
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

    for (int y = min_y; y <= max_y; ++y)
    {
        for (int x = min_x; x <= max_x; ++x)
        {
            LoadTile(tile_db, zoom, x,
                     y,
                     api_key);
        }
    }
}

static void DrawTiles(int zoom)
{
    for (int i = 0; i < tile_count; ++i)
    {
        MapTile *tile = &tile_cache[i];

        if (tile->zoom != zoom || tile->texture.id == 0)
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

static void DrawMarkers(int zoom)
{
    int color_count = (int)(sizeof(marker_colors) / sizeof(marker_colors[0]));

    for (int i = 0; i < marker_count; ++i)
    {
        Vector2 pos = LatLonToWorldPixel(
            markers[i].lat,
            markers[i].lon,
            zoom);

        int color_index = markers[i].type;
        if (color_index < 0 || color_index >= color_count)
        {
            color_index = 0;
        }

        DrawCircleV(pos, 8.0f, marker_colors[color_index]);
        DrawCircleLinesV(pos, 11.0f, BLACK);
    }
}

int main(void)
{
    TileDb tile_db; // Database

    if (!TileDbOpen(
            &tile_db,
            "data/map_cache.db",
            "db/schema.sql"))
    {
        fprintf(stderr, "Failed to open tile database\n");
        CloseWindow();
        return 1;
    }

    InitWindow(1280, 720, "MapBoard - Dynamic Map Tiles");
    SetTargetFPS(60);

    if (!DirectoryExists("tiles"))
    {
        MakeDirectory("tiles");
    }

    if (!DirectoryExists("data"))
    {
        MakeDirectory("data");
    }

    const char *api_key = getenv("MAPTILER_KEY");
    if (api_key == NULL || api_key[0] == '\0')
    {
        fprintf(
            stderr,
            "MAPTILER_KEY is not set. In PowerShell run:\n"
            "$env:MAPTILER_KEY=\"your-new-key\"\n");
        CloseWindow();
        return 1;
    }

    const double center_lat = 56.8332;
    const double center_lon = 13.9408;
    const int map_zoom = 14;

    Camera2D camera = {0};
    camera.target = LatLonToWorldPixel(center_lat, center_lon, map_zoom);
    camera.offset = (Vector2){
        GetScreenWidth() / 2.0f,
        GetScreenHeight() / 2.0f};
    camera.rotation = 0.0f;
    camera.zoom = 1.0f;

    LoadMarkers("data/markers_geo.txt");

    while (!WindowShouldClose())
    {
        float wheel = GetMouseWheelMove();
        if (wheel != 0.0f)
        {
            Vector2 mouse_before = GetScreenToWorld2D(GetMousePosition(), camera);

            camera.zoom += wheel * 0.1f;
            if (camera.zoom < 0.25f)
                camera.zoom = 0.25f;
            if (camera.zoom > 4.0f)
                camera.zoom = 4.0f;

            Vector2 mouse_after = GetScreenToWorld2D(GetMousePosition(), camera);
            camera.target.x += mouse_before.x - mouse_after.x;
            camera.target.y += mouse_before.y - mouse_after.y;
        }

        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT))
        {
            Vector2 delta = GetMouseDelta();
            camera.target.x -= delta.x / camera.zoom;
            camera.target.y -= delta.y / camera.zoom;
        }

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            marker_count < MAX_MARKERS)
        {
            Vector2 world = GetScreenToWorld2D(GetMousePosition(), camera);
            Marker *marker = &markers[marker_count];

            WorldPixelToLatLon(
                world,
                map_zoom,
                &marker->lat,
                &marker->lon);

            marker->type = chosen_type;
            marker->label[0] = '\0';
            marker_count++;
        }

        if (IsKeyPressed(KEY_ONE))
            chosen_type = 1;
        if (IsKeyPressed(KEY_TWO))
            chosen_type = 2;
        if (IsKeyPressed(KEY_THREE))
            chosen_type = 3;

        if (IsKeyPressed(KEY_S))
        {
            SaveMarkers("data/markers_geo.txt");
        }

        if (IsKeyPressed(KEY_L))
        {
            LoadMarkers("data/markers_geo.txt");
        }

        if (IsKeyPressed(KEY_X) || IsKeyPressed(KEY_DELETE))
        {
            int marker_index = FindMarkerNearMouse(
                GetMousePosition(),
                camera,
                map_zoom);

            if (marker_index != -1)
            {
                DeleteMarker(marker_index);
            }
        }

        UnloadDistantTiles(camera, map_zoom);
        EnsureVisibleTiles(camera, map_zoom, api_key, &tile_db);

        BeginDrawing();
        ClearBackground(BLACK);

        BeginMode2D(camera);
        DrawTiles(map_zoom);
        DrawMarkers(map_zoom);
        EndMode2D();

        DrawRectangle(10, 10, 520, 64, Fade(BLACK, 0.65f));
        DrawText(
            "Right drag: pan | Wheel: scale | Left click: marker",
            20,
            20,
            18,
            RAYWHITE);
        DrawText(
            TextFormat("Loaded tiles: %d | Markers: %d", tile_count, marker_count),
            20,
            47,
            18,
            RAYWHITE);

        EndDrawing();
    }

    for (int i = 0; i < tile_count; ++i)
    {
        if (tile_cache[i].texture.id != 0)
        {
            UnloadTexture(tile_cache[i].texture);
        }
    }

    TileDbClose(&tile_db);
    CloseWindow();
    return 0;
}
