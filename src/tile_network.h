#ifndef TILE_NETWORK_H
#define TILE_NETWORK_H

#include <stddef.h>

typedef enum
{
    TILE_DOWNLOAD_NONE,
    TILE_DOWNLOAD_ACTIVE,
    TILE_DOWNLOAD_COMPLETE,
    TILE_DOWNLOAD_FAILED
} TileDownloadState;

typedef struct
{
    int zoom;
    int x;
    int y;

    TileDownloadState state;

    unsigned char *data;
    size_t size;

    void *internal_handle;
} TileDownload;

int TileNetworkInit(void);

void TileNetworkShutdown(void);

int TileNetworkRequest(
    int zoom,
    int x,
    int y,
    const char *api_key
);

void TileNetworkUpdate(void);

#endif