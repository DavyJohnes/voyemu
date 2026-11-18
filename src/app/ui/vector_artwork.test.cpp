#include "vector_artwork.hpp"
#include "parameters_layout.hpp"
#include "ui_resources.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace voyemu;
namespace {
void require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

ui::VectorArtwork embedded(ui::Resource id) {
    const auto bytes = ui::resource(id);

    return ui::parse_vector_artwork(
            std::string_view(reinterpret_cast<const char *>(bytes.data()), bytes.size()));
}

// Alpha at an artwork point (viewBox units) in the theme or accent layer.
std::uint8_t alpha(const std::vector<std::uint8_t> &pixels, const ui::VectorArtwork &artwork,
                   int width, int height, float x, float y, bool accent) {
    const int column = int((x - artwork.x) * width / artwork.width),
              row = int((y - artwork.y) * height / artwork.height) +
                    (accent ? height + ui::artwork_layer_gap : 0);

    return pixels[(std::size_t(row) * width + column) * 4 + 3];
}
} // namespace

int main() {
    try {
        const auto text = embedded(ui::Resource::LogoText);
        require(std::abs(text.width / text.height - cluster::wordmark_aspect) < .01f,
                "wordmark bounds match the layout aspect");
        require(text.accent == std::array<std::uint8_t, 3>{0xff, 0x78, 0x1f},
                "the letter e keeps its own orange");
        constexpr int width = 356, height = 64;
        const auto pixels = ui::rasterize_artwork(text, width, height);
        require(pixels.size() == std::size_t(width) * (2 * height + ui::artwork_layer_gap) * 4,
                "two layers and a gap");
        require(alpha(pixels, text, width, height, 325, 499, false) == 255, "o ring is filled");
        require(alpha(pixels, text, width, height, 281, 499, false) == 0,
                "o counter stays open under even-odd fill");
        require(alpha(pixels, text, width, height, 540, 498, true) == 255 &&
                        alpha(pixels, text, width, height, 540, 498, false) == 0,
                "e is drawn only in the accent layer");
        require(alpha(pixels, text, width, height, 522, 480, true) == 0, "e eye stays open");
        const std::size_t e = (std::size_t(height + ui::artwork_layer_gap +
                                           int((498 - text.y) * height / text.height)) *
                                       width +
                               int((540 - text.x) * width / text.width)) *
                              4;
        require(pixels[e] == 0xff && pixels[e + 1] == 0x78 && pixels[e + 2] == 0x1f,
                "accent layer carries the accent colour");
        const float body = text.y + text.height * cluster::wordmark_body + 2;

        for (int column = 520; column < 900; column += 4) {
            const auto x = static_cast<float>(column);
            require(alpha(pixels, text, width, height, x, body, false) == 0 &&
                            alpha(pixels, text, width, height, x, body, true) == 0,
                    "only the y descender hangs below the letter bodies");
        }

        require(alpha(pixels, text, width, height, 385, 570, false) == 255, "y descender is drawn");
        const auto figure = embedded(ui::Resource::LogoFigure);
        require(std::abs(figure.width / figure.height - 2) < .01f,
                "flat mark keeps its 2:1 bounds without a backdrop");
        const auto mark = ui::rasterize_artwork(figure, 80, 40);
        require(alpha(mark, figure, 80, 40, 640, 560, true) == 255,
                "mark keeps its orange element");
        bool rejected = false;
        try {
            ui::parse_vector_artwork("<svg><circle r=\"5\"/></svg>");
        } catch (const std::runtime_error &) {
            rejected = true;
        }

        require(rejected, "artwork without filled paths is rejected");
        std::cout << "vector artwork passed\n";

        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';

        return 1;
    }
}
