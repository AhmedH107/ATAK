#include "geo.h"

#include <math.h>

#define TILE_SIZE 256

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

Vector2 LatLonToWorldPixel(double lat, double lon, int zoom)
{
    const double world_size = TILE_SIZE * pow(2.0, zoom);
    const double sin_lat = sin(lat * M_PI / 180.0);

    double x = (lon + 180.0) / 360.0 * world_size;
    double y = (0.5 - log((1.0 + sin_lat) / (1.0 - sin_lat)) /
                          (4.0 * M_PI)) *
               world_size;

    return (Vector2){(float)x, (float)y};
}

void WorldPixelToLatLon(
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

int NormalizeTileX(int x, int zoom)
{
    int tile_count_at_zoom = 1 << zoom;
    x %= tile_count_at_zoom;

    if (x < 0)
    {
        x += tile_count_at_zoom;
    }

    return x;
}