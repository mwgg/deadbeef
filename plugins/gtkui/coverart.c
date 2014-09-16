/*
    DeaDBeeF - The Ultimate Music Player
    Copyright (C) 2009-2013 Alexey Yakovenko <waker@users.sourceforge.net>

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
#include <gtk/gtk.h>
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <assert.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#include "coverart.h"
#include "../artwork/artwork.h"
#include "gtkui.h"

//#define trace(...) { fprintf(stderr, __VA_ARGS__); }
#define trace(...)

static DB_artwork_plugin_t *artwork_plugin;

static GdkPixbuf *pixbuf_default;

#define CACHE_SIZE 20

typedef struct {
    struct timeval tm;
    time_t file_time;
    char *fname;
    int width;
    GdkPixbuf *pixbuf;
} cached_pixbuf_t;

#define MAX_CALLBACKS 200

typedef struct cover_callback_s {
    void (*cb) (void*ud);
    void *ud;
} cover_callback_t;

typedef struct load_query_s {
    char *fname;
    int width;
    cover_callback_t callbacks[MAX_CALLBACKS];
    int numcb;
    struct load_query_s *next;
} load_query_t;

static cached_pixbuf_t cache[CACHE_SIZE];
static int terminate;
static uintptr_t mutex;
static uintptr_t cond;
static uintptr_t tid;
static load_query_t *queue;
static load_query_t *tail;

static void
queue_add (char *fname, const int width, void (*callback)(void *), void *user_data)
{
    trace("coverart: queue_add %s @ %d pixels\n", fname, width);
    load_query_t *q = malloc(sizeof(load_query_t));
    if (!q) {
        free(fname);
        if (callback) {
            callback(user_data);
        }
        return;
    }

    q->fname = fname;
    q->width = width;
    q->numcb = 1;
    q->callbacks[0].cb = callback;
    q->callbacks[0].ud = user_data;
    q->next = NULL;

    if (tail) {
        tail->next = q;
        tail = q;
    }
    else {
        queue = tail = q;
    }
    deadbeef->cond_signal (cond);
}

static void
queue_add_load (const char *fname, const int width, void (*callback)(void *), void *user_data)
{
    for (load_query_t *q = queue; q; q = q->next) {
        if (q->fname && !strcmp (q->fname, fname) && width == q->width) {
            trace("coverart: %s already in queue, add to callbacks\n", fname);
            if (q->numcb < MAX_CALLBACKS && callback) {
                q->callbacks[q->numcb].cb = callback;
                q->callbacks[q->numcb].ud = user_data;
                q->numcb++;
            }
            return;
        }
    }

    queue_add(strdup(fname), width, callback, user_data);
}

static void
queue_pop (void) {
    load_query_t *next = queue->next;
    if (queue->fname) {
        free (queue->fname);
    }
    free (queue);
    queue = next;
    if (!queue) {
        tail = NULL;
    }
}

static void
load_image(const load_query_t *query)
{
    char *fname_copy = strdup(query->fname);
    if (!fname_copy) {
        return;
    }

    /* Create a new pixbuf from this file */
    deadbeef->mutex_unlock(mutex);
    GdkPixbuf *pixbuf = NULL;
    struct stat stat_buf;
    if (!stat(query->fname, &stat_buf)) {
        GError *error = NULL;
        pixbuf = gdk_pixbuf_new_from_file_at_scale(query->fname, query->width, query->width, TRUE, &error);
        if (error) {
            //fprintf (stderr, "gdk_pixbuf_new_from_file_at_scale %s %d failed, error: %s\n", query->fname, query->width, error ? error->message : "n/a");
            g_error_free(error);
        }
    }
    if (!pixbuf) {
        pixbuf = cover_get_default_pixbuf();
    }
    trace("covercache: loaded pixbuf %s\n", query->fname);
    deadbeef->mutex_lock(mutex);

    /* Look for a blank slot in the pixbuf cache, or for the oldest entry */
    size_t cache_idx = 0;
    while (cache[cache_idx].pixbuf && cache_idx++ < CACHE_SIZE);
    if (cache_idx == CACHE_SIZE) {
        cache_idx = 0;
        struct timeval *min_time = &cache[0].tm;
        for (size_t i = 1; i < CACHE_SIZE; i++) {
            if (cache[i].tm.tv_sec < min_time->tv_sec || cache[i].tm.tv_sec == min_time->tv_sec && cache[i].tm.tv_usec < min_time->tv_usec) {
                cache_idx = i;
                min_time = &cache[i].tm;
            }
        }
        g_object_unref(cache[cache_idx].pixbuf);
        free(cache[cache_idx].fname);
    }

//            if (cache_idx == -1) {
//                trace ("coverart pixbuf cache overflow, waiting...\n");
//                usleep (500000);
//                deadbeef->mutex_lock(mutex);
//                continue;
//            }

    /* Set the pixbuf in the cache slot */
    cache[cache_idx].pixbuf = pixbuf;
    cache[cache_idx].fname = fname_copy;
    cache[cache_idx].file_time = stat_buf.st_mtime;
    gettimeofday(&cache[cache_idx].tm, NULL);
    cache[cache_idx].width = query->width;
}

static void
send_callbacks(const load_query_t *query)
{
    for (size_t i = 0; i < query->numcb; i++) {
        if (query->callbacks[i].cb) {
            trace("covercache: send callback for %s\n", query->fname);
            query->callbacks[i].cb(query->callbacks[i].ud);
        }
    }
}

static void
loading_thread (void *none) {
#ifdef __linux__
    prctl (PR_SET_NAME, "deadbeef-gtkui-artwork", 0, 0, 0, 0);
#endif

    deadbeef->mutex_lock(mutex);

    while (!terminate) {
        trace("covercache: waiting for signal...\n");
        pthread_cond_wait((pthread_cond_t *)cond, (pthread_mutex_t *)mutex);
        trace("covercache: signal received (terminate=%d, queue=%p)\n", terminate, queue);

        while (!terminate && queue) {
            if (queue->fname) {
                load_image(queue);
            }
            send_callbacks(queue);
            queue_pop();
        }
    }

    deadbeef->mutex_unlock(mutex);
}

typedef struct {
    int width;
    void (*callback)(void *user_data);
    void *user_data;
} cover_avail_info_t;

static void
cover_avail_callback (const char *fname, const char *artist, const char *album, void *user_data) {
    if (!fname) {
        free (user_data);
        return;
    }
    cover_avail_info_t *dt = user_data;
    // means requested image is now in disk cache
    // load it into main memory
    GdkPixbuf *pb = get_cover_art_callb (fname, artist, album, dt->width, dt->callback, dt->user_data);
    if (pb) {
        g_object_unref (pb);
    }
    free (dt);
}

static GdkPixbuf *
get_pixbuf (const char *fname, int width, void (*callback)(void *user_data), void *user_data) {
    /* Look in the pixbuf cache */
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (cache[i].pixbuf && !strcmp (fname, cache[i].fname) && cache[i].width == width) {
            struct stat stat_buf;
            if (!stat(fname, &stat_buf) && stat_buf.st_mtime == cache[i].file_time) {
                gettimeofday (&cache[i].tm, NULL);
                GdkPixbuf *pb = cache[i].pixbuf;
                g_object_ref (pb);
                return pb;
            }
            else {
                g_object_unref(cache[i].pixbuf);
                cache[i].pixbuf = NULL;
            }
        }
    }
#if 0
    printf ("cache miss: %s/%d\n", fname, width);
    for (int i = 0; i < CACHE_SIZE; i++) {
        if (cache[i].pixbuf) {
            printf ("    cache line %d: %s/%d\n", i, cache[i].fname, cache[i].width);
        }
    }
#endif

    /* Request to load this image into the pixbuf cache */
    queue_add_load(fname, width, callback, user_data);
    return NULL;
}

void
queue_cover_callback (void (*callback)(void *user_data), void *user_data) {
    if (callback) {
        deadbeef->mutex_lock (mutex);
        queue_add(NULL, -1, callback, user_data);
        deadbeef->mutex_unlock (mutex);
    }
}


GdkPixbuf *
get_cover_art_callb (const char *fname, const char *artist, const char *album, int width, void (*callback) (void *user_data), void *user_data) {
    if (!artwork_plugin) {
        return NULL;
    }

    if (width == -1) {
        char path[2048];
        artwork_plugin->make_cache_path2 (path, sizeof (path), fname, album, artist, -1);
        deadbeef->mutex_lock (mutex);
        int i_largest = -1;
        int size_largest = -1;
        for (int i = 0; i < CACHE_SIZE; i++) {
            if (!cache[i].pixbuf) {
                continue;
            }
            if (!strcmp (cache[i].fname, path)) {
                gettimeofday (&cache[i].tm, NULL);
                if (cache[i].width > size_largest) {
                    size_largest = cache[i].width;
                    i_largest = i;
                }
            }
        }
        if (i_largest != -1) {
            GdkPixbuf *pb = cache[i_largest].pixbuf;
            g_object_ref (pb);
            deadbeef->mutex_unlock (mutex);
            return pb;
        }
        deadbeef->mutex_unlock (mutex);
        return NULL;
    }

    cover_avail_info_t *dt = malloc (sizeof (cover_avail_info_t));
    dt->width = width;
    dt->callback = callback;
    dt->user_data = user_data;
    char *image_fname = artwork_plugin->get_album_art (fname, artist, album, -1, cover_avail_callback, dt);
    if (image_fname) {
        deadbeef->mutex_lock(mutex);
        GdkPixbuf *pb = get_pixbuf (image_fname, width, callback, user_data);
        deadbeef->mutex_unlock(mutex);
        free (image_fname);
        return pb;
    }
    return NULL;
}

void
coverart_reset_queue (void) {
    deadbeef->mutex_lock (mutex);
    if (queue) {
        load_query_t *q = queue->next;
        while (q) {
            load_query_t *next = q->next;
            if (q->fname) {
                free (q->fname);
            }
            free (q);
            q = next;
        }
        queue->next = NULL;
        tail = queue;
    }
    deadbeef->mutex_unlock (mutex);

    if (artwork_plugin) {
        artwork_plugin->reset (1);
    }
}

void
cover_art_init (void) {
    terminate = 0;
    mutex = deadbeef->mutex_create_nonrecursive ();
    cond = deadbeef->cond_create ();
    if (mutex && cond) {
        tid = deadbeef->thread_start_low_priority (loading_thread, NULL);
    }

    if (tid) {
        const DB_plugin_t *plugin = deadbeef->plug_get_for_id("artwork");
        if (plugin && PLUG_TEST_COMPAT(plugin, 1, 2)) {
            artwork_plugin = (DB_artwork_plugin_t *)plugin;
        }
    }
}

void
cover_art_disconnect (void) {
    if (artwork_plugin) {
        const DB_artwork_plugin_t *plugin = artwork_plugin;
        artwork_plugin = NULL;
        trace("resetting artwork plugin...\n");
        plugin->reset(0);
    }
}

void
cover_art_free (void) {
    trace ("terminating cover art loader...\n");

    if (tid) {
        deadbeef->mutex_lock(mutex);
        terminate = 1;
        trace("sending terminate signal to art loader thread...\n");
        deadbeef->cond_signal(cond);
        deadbeef->mutex_unlock(mutex);
        deadbeef->thread_join(tid);
        tid = 0;
    }

    while (queue) {
        queue_pop();
    }

    if (cond) {
        deadbeef->cond_free(cond);
        cond = 0;
    }
    if (mutex) {
        deadbeef->mutex_free(mutex);
        mutex = 0;
    }

    for (int i = 0; i < CACHE_SIZE; i++) {
        if (cache[i].pixbuf) {
            g_object_unref(cache[i].pixbuf);
        }
    }
    memset(cache, 0, sizeof(cache));
    if (pixbuf_default) {
        g_object_unref(pixbuf_default);
        pixbuf_default = NULL;
    }

    trace("Cover art objects all freed\n");
}

GdkPixbuf *
cover_get_default_pixbuf (void) {
    if (!artwork_plugin) {
        return NULL;
    }
    if (!pixbuf_default) {
        GError *error = NULL;
        const char *defpath = artwork_plugin->get_default_cover ();
        pixbuf_default = gdk_pixbuf_new_from_file (defpath, &error);
        if (!pixbuf_default) {
            fprintf (stderr, "default cover: gdk_pixbuf_new_from_file %s failed, error: %s\n", defpath, error->message);
        }
        if (error) {
            g_error_free (error);
            error = NULL;
        }
        if (!pixbuf_default) {
            pixbuf_default = gdk_pixbuf_new (GDK_COLORSPACE_RGB, FALSE, 8, 2, 2);
        }
        assert (pixbuf_default);
    }

    g_object_ref (pixbuf_default);
    return pixbuf_default;
}

int
gtkui_is_default_pixbuf (GdkPixbuf *pb) {
    return pb == pixbuf_default;
}
