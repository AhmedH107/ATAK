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
#define MIN_MAP_ZOOM 2
#define MAX_MAP_ZOOM 20

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
    int map_zoom = 14;

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

        // map_zoom = show what Z tiles.
        // camera.zoom = fine adjustment per tile
        if (wheel != 0.0f)
        {
            Vector2 mouse =
                GetMousePosition();

            // Remember the geographic point currently underneath the mouse.
            Vector2 mouse_world_before =
                GetScreenToWorld2D(
                    mouse,
                    camera);

            double mouse_lat = 0.0;
            double mouse_lon = 0.0;

            WorldPixelToLatLon(
                mouse_world_before,
                map_zoom,
                &mouse_lat,
                &mouse_lon);

            // change zoom here through wheel
            camera.zoom *=
                powf(1.15f, wheel);

            /*
             * One complete 2x enlargement means:
             *
             * Z14 -> Z15
             *
             * World coordinates double at every
             * Web Mercator zoom level.
             */
            while (camera.zoom >= 2.0f &&
                   map_zoom < MAX_MAP_ZOOM)
            {
                camera.zoom *= 0.5f;

                camera.target.x *= 2.0f;
                camera.target.y *= 2.0f;

                map_zoom++;
            }

            while (camera.zoom < 1.0f &&
                   map_zoom > MIN_MAP_ZOOM)
            {
                camera.zoom *= 2.0f;

                camera.target.x *= 0.5f;
                camera.target.y *= 0.5f;

                map_zoom--;
            }

            if (map_zoom == MAX_MAP_ZOOM &&
                camera.zoom > 2.0f)
            {
                camera.zoom = 2.0f;
            }

            if (map_zoom == MIN_MAP_ZOOM &&
                camera.zoom < 1.0f)
            {
                camera.zoom = 1.0f;
            }

            Vector2 desired_mouse_world =
                LatLonToWorldPixel(
                    mouse_lat,
                    mouse_lon,
                    map_zoom);

            Vector2 mouse_world_after =
                GetScreenToWorld2D(
                    mouse,
                    camera);

            camera.target.x +=
                desired_mouse_world.x -
                mouse_world_after.x;

            camera.target.y +=
                desired_mouse_world.y -
                mouse_world_after.y;
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
