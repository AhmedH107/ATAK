#include "tile_network.h"

#include <curl/curl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PENDING_DOWNLOADS 32
#define TILE_URL_CAPACITY 1024

typedef enum
{
    TILE_REQUEST_NONE,
    TILE_REQUEST_DOWNLOADING,
    TILE_REQUEST_READY

} TileRequestState;

typedef struct
{
    int in_use;

    int zoom;
    int x;
    int y;

    TileRequestState state;

    unsigned char *data;
    size_t size;

    CURL *curl;

    char url[TILE_URL_CAPACITY];

} TileRequest;

static TileRequest pending_downloads[MAX_PENDING_DOWNLOADS];

static CURLM *curl_multi = NULL;

static int curl_global_initialized = 0;

/* --------------------------------------------------------- */
/* Initialization                                            */
/* --------------------------------------------------------- */

static int EnsureInitialized(void)
{
    if (curl_multi != NULL)
    {
        return 1;
    }

    CURLcode global_result =
        curl_global_init(
            CURL_GLOBAL_DEFAULT);

    if (global_result != CURLE_OK)
    {
        fprintf(
            stderr,
            "curl_global_init failed: %s\n",
            curl_easy_strerror(
                global_result));

        return 0;
    }

    curl_global_initialized = 1;

    curl_multi =
        curl_multi_init();

    if (curl_multi == NULL)
    {
        fprintf(
            stderr,
            "curl_multi_init failed\n");

        curl_global_cleanup();
        curl_global_initialized = 0;

        return 0;
    }

    CURLMcode multi_result =
        curl_multi_setopt(
            curl_multi,
            CURLMOPT_MAX_TOTAL_CONNECTIONS,
            8L);

    if (multi_result != CURLM_OK)
    {
        fprintf(
            stderr,
            "Could not configure curl multi: %s\n",
            curl_multi_strerror(
                multi_result));

        curl_multi_cleanup(
            curl_multi);

        curl_multi = NULL;

        curl_global_cleanup();
        curl_global_initialized = 0;

        return 0;
    }

    return 1;
}

/* --------------------------------------------------------- */
/* Request pool                                              */
/* --------------------------------------------------------- */

static TileRequest *FindRequest(
    int zoom,
    int x,
    int y)
{
    for (int i = 0;
         i < MAX_PENDING_DOWNLOADS;
         ++i)
    {
        TileRequest *request =
            &pending_downloads[i];

        if (!request->in_use)
        {
            continue;
        }

        if (request->zoom == zoom &&
            request->x == x &&
            request->y == y)
        {
            return request;
        }
    }

    return NULL;
}

static TileRequest *AllocateRequest(void)
{
    for (int i = 0;
         i < MAX_PENDING_DOWNLOADS;
         ++i)
    {
        TileRequest *request =
            &pending_downloads[i];

        if (request->in_use)
        {
            continue;
        }

        memset(
            request,
            0,
            sizeof(*request));

        request->in_use = 1;

        return request;
    }

    return NULL;
}

int TileNetworkIsPending(
    int zoom,
    int x,
    int y)
{
    return FindRequest(
               zoom,
               x,
               y) != NULL;
}

/* --------------------------------------------------------- */
/* Download callback                                         */
/* --------------------------------------------------------- */

static size_t CurlWriteCallback(
    char *incoming,
    size_t size,
    size_t count,
    void *user_data)
{
    TileRequest *request =
        user_data;

    size_t incoming_size =
        size * count;

    if (incoming_size == 0)
    {
        return 0;
    }

    unsigned char *new_data =
        realloc(
            request->data,
            request->size +
                incoming_size);

    if (new_data == NULL)
    {
        return 0;
    }

    request->data =
        new_data;

    memcpy(
        request->data +
            request->size,
        incoming,
        incoming_size);

    request->size +=
        incoming_size;

    return incoming_size;
}

/* --------------------------------------------------------- */
/* Queue download                                            */
/* --------------------------------------------------------- */

int TileNetworkRequest(
    int zoom,
    int x,
    int y,
    const char *api_key)
{
    if (api_key == NULL ||
        api_key[0] == '\0')
    {
        return 0;
    }

    /*
     * Don't download the same tile twice.
     */
    if (FindRequest(
            zoom,
            x,
            y) != NULL)
    {
        return 1;
    }

    if (!EnsureInitialized())
    {
        return 0;
    }

    TileRequest *request =
        AllocateRequest();

    if (request == NULL)
    {
        return 0;
    }

    request->zoom = zoom;
    request->x = x;
    request->y = y;

    request->state =
        TILE_REQUEST_DOWNLOADING;

    int written = snprintf(
        request->url,
        sizeof(request->url),
        "https://api.maptiler.com/maps/"
        "streets-v2/256/"
        "%d/%d/%d.png?key=%s",
        zoom,
        x,
        y,
        api_key);

    if (written < 0 ||
        written >=
            (int)sizeof(request->url))
    {
        goto fail;
    }

    request->curl =
        curl_easy_init();

    if (request->curl == NULL)
    {
        goto fail;
    }

    CURLcode result;

    result = curl_easy_setopt(
        request->curl,
        CURLOPT_URL,
        request->url);

    if (result != CURLE_OK)
    {
        goto fail;
    }

    result = curl_easy_setopt(
        request->curl,
        CURLOPT_WRITEFUNCTION,
        CurlWriteCallback);

    if (result != CURLE_OK)
    {
        goto fail;
    }

    result = curl_easy_setopt(
        request->curl,
        CURLOPT_WRITEDATA,
        request);

    if (result != CURLE_OK)
    {
        goto fail;
    }

    result = curl_easy_setopt(
        request->curl,
        CURLOPT_PRIVATE,
        request);

    if (result != CURLE_OK)
    {
        goto fail;
    }

    result = curl_easy_setopt(
        request->curl,
        CURLOPT_FOLLOWLOCATION,
        1L);

    if (result != CURLE_OK)
    {
        goto fail;
    }

    result = curl_easy_setopt(
        request->curl,
        CURLOPT_FAILONERROR,
        1L);

    if (result != CURLE_OK)
    {
        goto fail;
    }

    /*
     * Avoid a dead request sitting around forever.
     */
    result = curl_easy_setopt(
        request->curl,
        CURLOPT_CONNECTTIMEOUT_MS,
        5000L);

    if (result != CURLE_OK)
    {
        goto fail;
    }

    result = curl_easy_setopt(
        request->curl,
        CURLOPT_TIMEOUT_MS,
        15000L);

    if (result != CURLE_OK)
    {
        goto fail;
    }

    CURLMcode multi_result =
        curl_multi_add_handle(
            curl_multi,
            request->curl);

    if (multi_result != CURLM_OK)
    {
        goto fail;
    }

    return 1;

fail:

    if (request->curl != NULL)
    {
        curl_easy_cleanup(
            request->curl);

        request->curl = NULL;
    }

    free(request->data);

    memset(
        request,
        0,
        sizeof(*request));

    return 0;
}

/* --------------------------------------------------------- */
/* Update downloads                                          */
/* --------------------------------------------------------- */

void TileNetworkUpdate(void)
{
    if (curl_multi == NULL)
    {
        return;
    }

    int running = 0;

    CURLMcode perform_result =
        curl_multi_perform(
            curl_multi,
            &running);

    if (perform_result != CURLM_OK)
    {
        fprintf(
            stderr,
            "curl_multi_perform failed: %s\n",
            curl_multi_strerror(
                perform_result));

        return;
    }

    CURLMsg *message = NULL;
    int messages_left = 0;

    while ((message =
                curl_multi_info_read(
                    curl_multi,
                    &messages_left)) != NULL)
    {
        if (message->msg !=
            CURLMSG_DONE)
        {
            continue;
        }

        CURL *easy =
            message->easy_handle;

        char *private_data = NULL;

        curl_easy_getinfo(
            easy,
            CURLINFO_PRIVATE,
            &private_data);

        TileRequest *request =
            (TileRequest *)private_data;

        long http_code = 0;

        curl_easy_getinfo(
            easy,
            CURLINFO_RESPONSE_CODE,
            &http_code);

        curl_multi_remove_handle(
            curl_multi,
            easy);

        curl_easy_cleanup(
            easy);

        if (request == NULL)
        {
            continue;
        }

        request->curl = NULL;

        if (message->data.result ==
                CURLE_OK &&
            http_code >= 200 &&
            http_code < 300 &&
            request->data != NULL &&
            request->size > 0)
        {
            request->state =
                TILE_REQUEST_READY;

            continue;
        }

        fprintf(
            stderr,
            "Tile download failed "
            "%d/%d/%d: curl=%s http=%ld\n",
            request->zoom,
            request->x,
            request->y,
            curl_easy_strerror(
                message->data.result),
            http_code);

        free(request->data);

        memset(
            request,
            0,
            sizeof(*request));
    }
}

/* --------------------------------------------------------- */
/* Completed downloads                                       */
/* --------------------------------------------------------- */

int TileNetworkTakeCompleted(
    TileDownloadResult *result)
{
    if (result == NULL)
    {
        return 0;
    }

    memset(
        result,
        0,
        sizeof(*result));

    for (int i = 0;
         i < MAX_PENDING_DOWNLOADS;
         ++i)
    {
        TileRequest *request =
            &pending_downloads[i];

        if (!request->in_use ||
            request->state !=
                TILE_REQUEST_READY)
        {
            continue;
        }

        result->zoom =
            request->zoom;

        result->x =
            request->x;

        result->y =
            request->y;

        /*
         * Transfer ownership of this allocation
         * to the caller.
         */
        result->data =
            request->data;

        result->size =
            request->size;

        request->data = NULL;

        memset(
            request,
            0,
            sizeof(*request));

        return 1;
    }

    return 0;
}

/* --------------------------------------------------------- */
/* Shutdown                                                  */
/* --------------------------------------------------------- */

void TileNetworkShutdown(void)
{
    for (int i = 0;
         i < MAX_PENDING_DOWNLOADS;
         ++i)
    {
        TileRequest *request =
            &pending_downloads[i];

        if (request->curl != NULL)
        {
            if (curl_multi != NULL)
            {
                curl_multi_remove_handle(
                    curl_multi,
                    request->curl);
            }

            curl_easy_cleanup(
                request->curl);

            request->curl = NULL;
        }

        free(request->data);

        memset(
            request,
            0,
            sizeof(*request));
    }

    if (curl_multi != NULL)
    {
        curl_multi_cleanup(
            curl_multi);

        curl_multi = NULL;
    }

    if (curl_global_initialized)
    {
        curl_global_cleanup();

        curl_global_initialized = 0;
    }
}