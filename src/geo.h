#ifndef GEO_H
#define GEO_H

#include "raylib.h"

Vector2 LatLonToWorldPixel(
    double lat,
    double lon,
    int zoom);

void WorldPixelToLatLon(
    Vector2 pixel,
    int zoom,
    double *lat,
    double *lon);

int NormalizeTileX(
    int x,
    int zoom);

#endif