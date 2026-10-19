#ifndef TILE_NETWORK_H
#define TILE_NETWORK_H

#include <stddef.h>

typedef struct
{
    int zoom;
    int x;
    int y;

    unsigned char *data;
    size_t size;

} TileDownloadResult;

/*
 * Returns non-zero if this tile already has
 * an active or completed network request.
 */
int TileNetworkIsPending(
    int zoom,
    int x,
    int y);

/*
 * Queue a tile for asynchronous downloading.
 *
 * Returns non-zero if the request is queued
 * or was already pending.
 */
int TileNetworkRequest(
    int zoom,
    int x,
    int y,
    const char *api_key);

void TileNetworkUpdate(void);

int TileNetworkTakeCompleted(
    TileDownloadResult *result);

void TileNetworkShutdown(void);

#endif