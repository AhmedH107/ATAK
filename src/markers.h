#ifndef MARKERS_H
#define MARKERS_H

#include "raylib.h"

#define MAX_MARKERS 1024

typedef struct
{
    double lat;
    double lon;
    int type;
    char label[64];
} Marker;

void MarkersLoad(const char *path);
void MarkersSave(const char *path);

void MarkersAdd(
    double lat,
    double lon,
    int type);

void MarkersDeleteNear(
    Vector2 mouse,
    Camera2D camera,
    int zoom);

void MarkersDraw(int zoom);

int MarkersGetCount(void);

#endif