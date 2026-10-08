#ifndef TILE_MANAGER_H
#define TILE_MANAGER_H

#include "raylib.h"
#include "tile_db.h"


void TileManagerUpdate(
    Camera2D camera,
    int zoom,
    TileDb *tile_db,
    const char *api_key
);


void TileManagerDraw(int zoom);


void TileManagerShutdown(void);


int TileManagerGetLoadedCount(void);

#endif