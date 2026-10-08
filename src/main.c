#include "raylib.h"
#include "tile_db.h"
#include "geo.h"
#include "markers.h"
#include "tile_manager.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DELETE_RADIUS 18.0f
#define MAX_MARKERS 1024

static const Color marker_colors[] = {
    GRAY,
    RED,
    BLUE,
    ORANGE};

int main(void)
{
    if (!DirectoryExists("data"))
    {
        MakeDirectory("data");
    }

    SetTargetFPS(60);

    InitWindow(
        1280,
        720,
        "MapBoard - Dynamic Map Tiles");

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

    MarkersLoad("data/markers_geo.txt");

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

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            Vector2 world =
                GetScreenToWorld2D(
                    GetMousePosition(),
                    camera);

            double lat = 0.0;
            double lon = 0.0;

            WorldPixelToLatLon(
                world,
                map_zoom,
                &lat,
                &lon);

            MarkersAdd(
                lat,
                lon,
                1);
        }

        if (IsKeyPressed(KEY_S))
        {
            MarkersSave("data/markers_geo.txt");
        }

        if (IsKeyPressed(KEY_L))
        {
            MarkersLoad("data/markers_geo.txt");
        }

        if (IsKeyPressed(KEY_X) || IsKeyPressed(KEY_DELETE))
        {
            MarkersDeleteNear(
                GetMousePosition(),
                camera,
                map_zoom);
        }

        TileManagerUpdate(
            camera,
            map_zoom,
            &tile_db,
            api_key);

        TileManagerUpdate(
            camera,
            map_zoom,
            &tile_db,
            api_key);

        BeginDrawing();
        ClearBackground(BLACK);

        BeginMode2D(camera);
        TileManagerDraw(map_zoom);
        MarkersDraw(map_zoom);
        EndMode2D();

        DrawRectangle(10, 10, 520, 64, Fade(BLACK, 0.65f));
        DrawText(
            "Right drag: pan | Wheel: scale | Left click: marker",
            20,
            20,
            18,
            RAYWHITE);

        TextFormat(
            "Loaded tiles: %d | Markers: %d",
            TileManagerGetLoadedCount(),
            MarkersGetCount());

        MarkersDraw(map_zoom);

        EndDrawing();
    }

    TileManagerShutdown();

    TileDbClose(&tile_db);

    CloseWindow();

    return 0;
}
