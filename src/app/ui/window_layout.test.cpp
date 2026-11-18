#include "window_layout.h"
#include <cassert>
#include <iostream>

int main() {
    VoyemuWindowRect screen{100, 50, 1512, 900}, media{0, 0, 960, 420}, cluster{0, 0, 1152, 388};

    media = voyemu_media_position(screen, media);
    cluster = voyemu_cluster_position(screen, media, cluster);
    assert(media.x == 100 && media.y == 530 && cluster.x == 280 && cluster.y == 130 &&
           voyemu_pair_fits(screen, media, cluster));
    screen = {-1920, 0, 1920, 1080};
    media = voyemu_media_position(screen, media);
    cluster = voyemu_cluster_position(screen, media, cluster);
    assert(voyemu_pair_fits(screen, media, cluster) && media.x < 0 && cluster.x < 0);
    screen = {0, 0, 1280, 700};
    media = voyemu_media_position(screen, media);
    cluster = voyemu_cluster_position(screen, media, cluster);
    assert(cluster.y == 0 && !voyemu_pair_fits(screen, media, cluster));
    auto wide = cluster;
    wide.width = 2000;
    assert(!voyemu_pair_fits(screen, media, wide));
    screen = {0, 0, 1512, 944};
    media = voyemu_media_position(screen, {0, 0, 1920, 388});
    assert(media.x == 0 && media.y == 556 && media.width == 1920 && media.height == 388);
    assert(!voyemu_rect_valid({0, 0, NAN, 20}) && !voyemu_rect_valid({0, 0, 10, -1}));
    std::cout << "paired window geometry, distinct monitor origins and fit limits passed\n";
}
