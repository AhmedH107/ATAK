#include "raylib.h"
#include <stdio.h>


#define DELETE_RADIUS 18.0f
#define MAX_MARKERS 1024

typedef struct {
    Vector2 pos; 
    int type;
    char label[64];   
} Marker;



static Marker markers[MAX_MARKERS];
static int marker_count = 0;
static int chosenType = 1;
const Color colourArray[] = {
    GRAY,
    RED,
    BLUE,
    ORANGE
};

void SaveMarkers(const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    for (int i = 0; i < marker_count; i++) {
        fprintf(f, "%f %f %d\n",
        markers[i].pos.x,
        markers[i].pos.y,
        markers[i].type);
    }

    fclose(f);
}

void LoadMarkers(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return;

    marker_count = 0;

    while (marker_count < MAX_MARKERS) {
        float x, y;
        int type;

        if (fscanf(f, "%f %f %d", &x, &y, &type) != 3) break;

        markers[marker_count].pos = (Vector2){ x, y };
        markers[marker_count].type = type;

        marker_count++;
    }

    fclose(f);
}

int FindMarkerNearMouse(Vector2 mouse, Camera2D camera) {
    int best_index = -1;
    float best_dist2 = DELETE_RADIUS * DELETE_RADIUS;

    for (int i = 0; i < marker_count; i++) {
        Vector2 screen_pos = GetWorldToScreen2D(markers[i].pos, camera);

        float dx = mouse.x - screen_pos.x;
        float dy = mouse.y - screen_pos.y;
        float dist2 = dx * dx + dy * dy;

        if (dist2 <= best_dist2) {
            best_dist2 = dist2;
            best_index = i;
        }
    }

    return best_index;
}

void DeleteMarker(int index) {
    if (index < 0 || index >= marker_count) return;

    for (int i = index; i < marker_count - 1; i++) {
        markers[i] = markers[i + 1];
    }

    marker_count--;
}

int main(void) {
    InitWindow(1280, 720, "MapBoard - Tactical Map Notebook Prototype");
    SetTargetFPS(60);


    Texture2D map = LoadTexture("assets/moon.png");

    Camera2D camera = { 0 };
    camera.target = (Vector2){ map.width / 2.0f, map.height / 2.0f };
    camera.offset = (Vector2){ 640, 360 };
    camera.rotation = 0.0f;
    camera.zoom = 1.0f;

    LoadMarkers("data/markers.txt");

    while (!WindowShouldClose()) {
        // Zoom
        float wheel = GetMouseWheelMove();
        if (wheel != 0) {
            camera.zoom += wheel * 0.1f;

            if (camera.zoom < 0.2f) camera.zoom = 0.2f;
            if (camera.zoom > 5.0f) camera.zoom = 5.0f;
        }

        // Pan with right mouse drag
        if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
            Vector2 delta = GetMouseDelta();
            camera.target.x -= delta.x / camera.zoom;
            camera.target.y -= delta.y / camera.zoom;
        }

        // Add marker with left click
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (marker_count < MAX_MARKERS) {
                Vector2 mouse = GetMousePosition();
                Vector2 world = GetScreenToWorld2D(mouse, camera);

                markers[marker_count].pos = world;
                markers[marker_count].type = chosenType;
                marker_count++;
            }
        }

        if (IsKeyPressed(KEY_ONE)) {
            chosenType = 1;
        }

        if (IsKeyPressed(KEY_TWO)) {
            chosenType = 2;
        }

        if (IsKeyPressed(KEY_THREE)) {
            chosenType = 3;
        }

        // Save/load
        if (IsKeyPressed(KEY_S)) {
            SaveMarkers("data/markers.txt");
        }

        if (IsKeyPressed(KEY_L)) {
            LoadMarkers("data/markers.txt");
        }

        if (IsKeyPressed(KEY_X) || IsKeyPressed(KEY_DELETE)) {
        Vector2 mouse = GetMousePosition();
        int marker_index = FindMarkerNearMouse(mouse, camera);

        if (marker_index != -1) {
        DeleteMarker(marker_index);
        }
}

        BeginDrawing();
        ClearBackground(RAYWHITE);

        BeginMode2D(camera);

        DrawTexture(map, 0, 0, WHITE);

        //draws markers
        for (int i = 0; i < marker_count; i++) {
            DrawCircleV(markers[i].pos, 8, colourArray[markers[i].type]);
            DrawCircleLinesV(markers[i].pos, 12, BLACK);
        }

        EndMode2D();

        DrawText("Left click: marker | X: delete |Right drag: pan | Wheel: zoom | S: save | L: load", 20, 20, 20, RED);
        DrawText(TextFormat("Markers: %d", marker_count), 20, 50, 20, DARKGRAY);

        EndDrawing();
    }

    UnloadTexture(map);
    CloseWindow();

    return 0;
}