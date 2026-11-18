#pragma once
#include <math.h>
#include <stdbool.h>

typedef struct {
    double x, y, width, height;
} VoyemuWindowRect;

static inline bool voyemu_rect_valid(VoyemuWindowRect r) {
    return isfinite(r.x) && isfinite(r.y) && isfinite(r.width) && isfinite(r.height) &&
           r.width > 0 && r.height > 0;
}

static inline VoyemuWindowRect voyemu_media_position(VoyemuWindowRect screen,
                                                     VoyemuWindowRect window) {
    window.x = screen.x;
    window.y = screen.y + screen.height - window.height;

    return window;
}

static inline VoyemuWindowRect
voyemu_cluster_position(VoyemuWindowRect screen, VoyemuWindowRect media, VoyemuWindowRect cluster) {
    cluster.x = screen.x + (screen.width - cluster.width) / 2;
    cluster.y = media.y - 12 - cluster.height;

    if (cluster.y < screen.y) {
        cluster.y = screen.y;
    }

    return cluster;
}

static inline bool voyemu_pair_fits(VoyemuWindowRect screen, VoyemuWindowRect media,
                                    VoyemuWindowRect cluster) {
    return voyemu_rect_valid(screen) && voyemu_rect_valid(media) && voyemu_rect_valid(cluster) &&
           media.x >= screen.x && media.x + media.width <= screen.x + screen.width &&
           cluster.x >= screen.x && cluster.x + cluster.width <= screen.x + screen.width &&
           media.y >= screen.y && media.y + media.height <= screen.y + screen.height &&
           cluster.y >= screen.y && cluster.y + cluster.height + 12 <= media.y;
}
