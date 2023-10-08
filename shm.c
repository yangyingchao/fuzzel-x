#include "shm.h"

#include <unistd.h>
#include <assert.h>

#include <sys/types.h>
#include <sys/mman.h>
#include <fcntl.h>

#include <tllist.h>

#define LOG_MODULE "shm"
#include "log.h"
#include "stride.h"

static tll(struct buffer) buffers;

static void
buffer_release(void *data, struct wl_buffer *wl_buffer)
{
    //printf("buffer release\n");
    struct buffer *buffer = data;
    assert(buffer->wl_buf == wl_buffer);
    assert(buffer->busy);
    buffer->busy = false;
}

static const struct wl_buffer_listener buffer_listener = {
    .release = &buffer_release,
};

struct buffer *
shm_get_buffer(struct wl_shm *shm, int width, int height)
{
    tll_foreach(buffers, it) {
        if (!it->item.busy &&
            it->item.width == width &&
            it->item.height == height)
        {
            it->item.busy = true;
            return &it->item;
        }
    }

    /*
     * No existing buffer available. Create a new one by:
     *
     * 1. open a memory backed "file" with memfd_create()
     * 2. mmap() the memory file, to be used by the cairo surface
     * 3. create a wayland shm buffer for the same memory file
     *
     * The cairo surface and the wayland buffer are now sharing
     * memory.
     */

    int pool_fd = -1;
    void *mmapped = NULL;
    size_t size = 0;

    struct wl_shm_pool *pool = NULL;
    struct wl_buffer *buf = NULL;

    pixman_image_t *pix = NULL;

#if defined(FUZZEL_ENABLE_CAIRO)
    cairo_surface_t *cairo_surface = NULL;
    cairo_t *cairo = NULL;
#endif

    /* Backing memory for SHM */
#if defined(MEMFD_CREATE)
    pool_fd = memfd_create("fuzzel-wayland-shm-buffer-pool", MFD_CLOEXEC);
#elif defined(__FreeBSD__)
    // memfd_create on FreeBSD 13 is SHM_ANON without sealing support
    pool_fd = shm_open(SHM_ANON, O_RDWR | O_CLOEXEC, 0600);
#else
    char name[] = "/tmp/fuzzel-wayland-shm-buffer-pool-XXXXXX";
    pool_fd = mkostemp(name, O_CLOEXEC);
    unlink(name);
#endif
    if (pool_fd == -1) {
        LOG_ERRNO("failed to create SHM backing memory file");
        goto err;
    }

    /* Total size */
    const uint32_t stride = stride_for_format_and_width(PIXMAN_a8r8g8b8, width);
    size = stride * height;
    if (ftruncate(pool_fd, size) == -1) {
        LOG_ERRNO("failed to truncate SHM pool");
        goto err;
    }

    mmapped = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, pool_fd, 0);
    if (mmapped == MAP_FAILED) {
        LOG_ERR("failed to mmap SHM backing memory file");
        goto err;
    }

#if defined(MEMFD_CREATE)
    /* Seal file - we no longer allow any kind of resizing */
    /* TODO: wayland mmaps(PROT_WRITE), for some unknown reason, hence we cannot use F_SEAL_FUTURE_WRITE */
    if (fcntl(pool_fd, F_ADD_SEALS,
              F_SEAL_GROW | F_SEAL_SHRINK | /*F_SEAL_FUTURE_WRITE |*/ F_SEAL_SEAL) < 0)
    {
        LOG_ERRNO("failed to seal SHM backing memory file");
        /* This is not a fatal error */
    }
#endif

    pool = wl_shm_create_pool(shm, pool_fd, size);
    if (pool == NULL) {
        LOG_ERR("failed to create SHM pool");
        goto err;
    }

    buf = wl_shm_pool_create_buffer(
        pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
    if (buf == NULL) {
        LOG_ERR("failed to create SHM buffer");
        goto err;
    }

    /* We use the entire pool for our single buffer */
    wl_shm_pool_destroy(pool); pool = NULL;
    close(pool_fd); pool_fd = -1;

    pix = pixman_image_create_bits_no_clear(
        PIXMAN_a8r8g8b8, width, height, mmapped, stride);
    if (pix == NULL) {
        LOG_ERR("failed to create pixman image");
        goto err;
    }

#if defined(FUZZEL_ENABLE_CAIRO)
    /* Create a cairo surface around the mmapped memory */
    cairo_surface = cairo_image_surface_create_for_data(
        mmapped, CAIRO_FORMAT_ARGB32, width, height, stride);
    if (cairo_surface_status(cairo_surface) != CAIRO_STATUS_SUCCESS) {
        LOG_ERR("failed to create cairo surface: %s",
                cairo_status_to_string(cairo_surface_status(cairo_surface)));
        goto err;
    }

    cairo = cairo_create(cairo_surface);
    if (cairo_status(cairo) != CAIRO_STATUS_SUCCESS) {
        LOG_ERR("failed to create cairo context: %s",
                cairo_status_to_string(cairo_status(cairo)));
        goto err;
    }
#endif

    /* Push to list of available buffers, but marked as 'busy' */
    tll_push_back(
        buffers,
        ((struct buffer){
            .width = width,
            .height = height,
            .stride = stride,
            .busy = true,
            .size = size,
            .mmapped = mmapped,
            .wl_buf = buf,
            .pix = pix,

#if defined(FUZZEL_ENABLE_CAIRO)
            .cairo_surface = cairo_surface,
            .cairo = cairo
#endif
        })
        );

    struct buffer *ret = &tll_back(buffers);
    wl_buffer_add_listener(ret->wl_buf, &buffer_listener, ret);
    return ret;

err:
#if defined(FUZZEL_ENABLE_CAIRO)
    if (cairo != NULL)
        cairo_destroy(cairo);
    if (cairo_surface != NULL)
        cairo_surface_destroy(cairo_surface);
#endif
    if (pix != NULL)
        pixman_image_unref(pix);
    if (buf != NULL)
        wl_buffer_destroy(buf);
    if (pool != NULL)
        wl_shm_pool_destroy(pool);
    if (pool_fd != -1)
        close(pool_fd);
    if (mmapped != NULL)
        munmap(mmapped, size);

    return NULL;
}

void
shm_fini(void)
{
    tll_foreach(buffers, it) {
        struct buffer *buf = &it->item;

#if defined(FUZZEL_ENABLE_CAIRO)
        cairo_destroy(buf->cairo);
        cairo_surface_destroy(buf->cairo_surface);
#endif

        pixman_image_unref(buf->pix);
        wl_buffer_destroy(buf->wl_buf);
        munmap(buf->mmapped, buf->size);

        tll_remove(buffers, it);
    }
}
