#include "markers.h"
#include "geo.h"

#include <stdio.h>

#define DELETE_RADIUS 18.0f

static Marker markers[MAX_MARKERS];
static int marker_count = 0;

// Avoids -Wpedantic warnings from Raylib colour macros
static const Color marker_colors[] = {
    {130, 130, 130, 255}, /* gray */
    {230, 41, 55, 255},   /* red */
    {0, 121, 241, 255},   /* blue */
    {255, 161, 0, 255}    /* orange */
};

void MarkersLoad(const char *path)
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

        if (fscanf(
                file,
                "%lf %lf %d",
                &lat,
                &lon,
                &type) != 3)
        {
            break;
        }

        Marker *marker = &markers[marker_count];

        marker->lat = lat;
        marker->lon = lon;
        marker->type = type;
        marker->label[0] = '\0';

        marker_count++;
    }

    fclose(file);
}

void MarkersSave(const char *path)
{
    FILE *file = fopen(path, "w");

    if (file == NULL)
    {
        fprintf(
            stderr,
            "Could not save markers to %s\n",
            path);

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

void MarkersAdd(
    double lat,
    double lon,
    int type)
{
    if (marker_count >= MAX_MARKERS)
    {
        fprintf(stderr, "Marker limit reached\n");
        return;
    }

    Marker *marker = &markers[marker_count];

    marker->lat = lat;
    marker->lon = lon;
    marker->type = type;
    marker->label[0] = '\0';

    marker_count++;
}

void MarkersDeleteNear(
    Vector2 mouse,
    Camera2D camera,
    int zoom)
{
    int best_index = -1;

    float best_distance_squared =
        DELETE_RADIUS * DELETE_RADIUS;

    for (int i = 0; i < marker_count; ++i)
    {
        Vector2 world_position =
            LatLonToWorldPixel(
                markers[i].lat,
                markers[i].lon,
                zoom);

        Vector2 screen_position =
            GetWorldToScreen2D(
                world_position,
                camera);

        float dx =
            mouse.x - screen_position.x;

        float dy =
            mouse.y - screen_position.y;

        float distance_squared =
            dx * dx + dy * dy;

        if (distance_squared <= best_distance_squared)
        {
            best_distance_squared =
                distance_squared;

            best_index = i;
        }
    }

    // Nothing close enough
    if (best_index == -1)
    {
        return;
    }

    for (int i = best_index;
         i < marker_count - 1;
         ++i)
    {
        markers[i] = markers[i + 1];
    }

    marker_count--;
}

void MarkersDraw(int zoom)
{
    const int color_count =
        (int)(sizeof(marker_colors) /
              sizeof(marker_colors[0]));

    for (int i = 0; i < marker_count; ++i)
    {
        Vector2 position =
            LatLonToWorldPixel(
                markers[i].lat,
                markers[i].lon,
                zoom);

        int color_index =
            markers[i].type;

        if (color_index < 0 ||
            color_index >= color_count)
        {
            color_index = 0;
        }

        DrawCircleV(
            position,
            8.0f,
            marker_colors[color_index]);

        DrawCircleLinesV(
            position,
            11.0f,
            BLACK);
    }
}

int MarkersGetCount(void)
{
    return marker_count;
}