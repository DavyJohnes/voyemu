#include "vector_artwork.hpp"
#include "error.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>
#include <string>
namespace voyemu::ui {
namespace {
using Point = std::array<float, 2>;
constexpr double pi = 3.14159265358979323846;
class PathData {
public:
    explicit PathData(std::string_view text) : text_(text) {}

    std::vector<std::vector<Point>> contours() {
        char command = 0;

        while (true) {
            skip();

            if (index_ >= text_.size()) {
                break;
            }

            if (std::isalpha(static_cast<unsigned char>(text_[index_]))) {
                command = text_[index_++];
            } else if (!command) {
                throw VoyemuError("UI_ARTWORK", "path data must start with a command");
            }

            segment(command);

            if (command == 'M') {
                command = 'L';
            } else if (command == 'm') {
                command = 'l';
            }
        }

        finish();

        return std::move(contours_);
    }

private:
    void skip() {
        while (index_ < text_.size() &&
               (text_[index_] == ' ' || text_[index_] == ',' || text_[index_] == '\n' ||
                text_[index_] == '\r' || text_[index_] == '\t')) {
            ++index_;
        }
    }

    double number() {
        skip();
        double sign = 1, value = 0, scale = 1;

        if (index_ < text_.size() && (text_[index_] == '-' || text_[index_] == '+')) {
            sign = text_[index_++] == '-' ? -1 : 1;
        }

        bool digits = false, dot = false;

        while (index_ < text_.size()) {
            const char c = text_[index_];

            if (c >= '0' && c <= '9') {
                digits = true;

                if (dot) {
                    scale /= 10;
                    value += (c - '0') * scale;
                } else {
                    value = value * 10 + (c - '0');
                }
            } else if (c == '.' && !dot) {
                dot = true;
            } else {
                break;
            }

            ++index_;
        }

        if (!digits) {
            throw VoyemuError("UI_ARTWORK", "number expected in path data");
        }

        if (index_ < text_.size() && (text_[index_] == 'e' || text_[index_] == 'E')) {
            ++index_;
            value *= std::pow(10.0, number());
        }

        return sign * value;
    }

    bool flag() {
        skip();

        if (index_ >= text_.size() || (text_[index_] != '0' && text_[index_] != '1')) {
            throw VoyemuError("UI_ARTWORK", "arc flag expected in path data");
        }

        return text_[index_++] == '1';
    }

    void line(double x, double y) {
        if (contour_.empty()) {
            contour_.push_back({float(x_), float(y_)});
        }

        x_ = x;
        y_ = y;
        contour_.push_back({float(x), float(y)});
    }

    void finish() {
        if (contour_.size() > 2) {
            contours_.push_back(std::move(contour_));
        }

        contour_.clear();
    }

    void cubic(double x1, double y1, double x2, double y2, double x, double y) {
        const double x0 = x_, y0 = y_;

        for (int i = 1; i <= 16; ++i) {
            const double t = i / 16.0, u = 1 - t;
            line(u * u * u * x0 + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t * x,
                 u * u * u * y0 + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t * y);
        }

        control_x_ = x2;
        control_y_ = y2;
    }

    // SVG 1.1 implementation notes F.6.5: endpoint to centre parameterization.
    void arc(double rx, double ry, double rotation, bool large, bool sweep, double x, double y) {
        const double x1 = x_, y1 = y_;

        rx = std::abs(rx);
        ry = std::abs(ry);

        if (rx == 0 || ry == 0 || (x1 == x && y1 == y)) {
            line(x, y);

            return;
        }

        const double phi = rotation * pi / 180, c = std::cos(phi), s = std::sin(phi);
        const double dx = (x1 - x) / 2, dy = (y1 - y) / 2;
        const double px = c * dx + s * dy, py = -s * dx + c * dy;
        const double lambda = px * px / (rx * rx) + py * py / (ry * ry);

        if (lambda > 1) {
            rx *= std::sqrt(lambda);
            ry *= std::sqrt(lambda);
        }

        const double numerator = rx * rx * ry * ry - rx * rx * py * py - ry * ry * px * px,
                     denominator = rx * rx * py * py + ry * ry * px * px;
        const double coefficient = (large == sweep ? -1 : 1) *
                                   std::sqrt(std::max(0.0, numerator / denominator));
        const double cx_ = coefficient * rx * py / ry, cy_ = -coefficient * ry * px / rx;
        const double cx = c * cx_ - s * cy_ + (x1 + x) / 2, cy = s * cx_ + c * cy_ + (y1 + y) / 2;
        const double start = std::atan2((py - cy_) / ry, (px - cx_) / rx);
        double delta = std::atan2((-py - cy_) / ry, (-px - cx_) / rx) - start;

        if (sweep && delta < 0) {
            delta += 2 * pi;
        } else if (!sweep && delta > 0) {
            delta -= 2 * pi;
        }

        const int steps = std::max(4, int(std::ceil(std::abs(delta) / (2 * pi) * 96)));

        for (int i = 1; i < steps; ++i) {
            const double angle = start + delta * i / steps;
            const double ex = rx * std::cos(angle), ey = ry * std::sin(angle);
            line(cx + c * ex - s * ey, cy + s * ex + c * ey);
        }

        line(x, y);
    }

    void segment(char command) {
        const bool relative = std::islower(static_cast<unsigned char>(command));
        const double ox = relative ? x_ : 0, oy = relative ? y_ : 0;
        const char kind = char(std::toupper(static_cast<unsigned char>(command)));

        if (kind != 'Z' && closed_) {
            // A segment after closepath starts a new contour at the closed contour's start.
            closed_ = false;

            if (kind != 'M') {
                x_ = start_x_;
                y_ = start_y_;
            }
        }

        const bool smooth = kind == 'S' && (previous_ == 'C' || previous_ == 'S');

        switch (kind) {
        case 'M': {
            finish();
            const double x = ox + number(), y = oy + number();
            x_ = start_x_ = x;
            y_ = start_y_ = y;
            break;
        }
        case 'L': {
            const double x = ox + number(), y = oy + number();
            line(x, y);
            break;
        }
        case 'H':
            line(ox + number(), y_);
            break;
        case 'V':
            line(x_, oy + number());
            break;
        case 'C': {
            const double x1 = ox + number(), y1 = oy + number(), x2 = ox + number(),
                         y2 = oy + number(), x = ox + number(), y = oy + number();
            cubic(x1, y1, x2, y2, x, y);
            break;
        }
        case 'S': {
            const double x1 = smooth ? 2 * x_ - control_x_ : x_,
                         y1 = smooth ? 2 * y_ - control_y_ : y_;
            const double x2 = ox + number(), y2 = oy + number(), x = ox + number(),
                         y = oy + number();
            cubic(x1, y1, x2, y2, x, y);
            break;
        }
        case 'A': {
            const double rx = number(), ry = number(), rotation = number();
            const bool large = flag(), sweep = flag();
            const double x = ox + number(), y = oy + number();
            arc(rx, ry, rotation, large, sweep, x, y);
            break;
        }
        case 'Z':
            finish();
            x_ = start_x_;
            y_ = start_y_;
            closed_ = true;
            break;
        default:
            throw VoyemuError("UI_ARTWORK", "unsupported path command");
        }

        previous_ = kind;
    }

    std::string_view text_;
    std::size_t index_{};
    double x_{}, y_{}, start_x_{}, start_y_{}, control_x_{}, control_y_{};
    char previous_{};
    bool closed_{};
    std::vector<Point> contour_;
    std::vector<std::vector<Point>> contours_;
};

std::string_view attribute(std::string_view tag, std::string_view name) {
    for (std::size_t at = tag.find(name); at != std::string_view::npos;
         at = tag.find(name, at + 1)) {
        const auto quote = at + name.size();

        if ((at == 0 || tag[at - 1] == ' ') && quote + 1 < tag.size() && tag[quote] == '=' &&
            tag[quote + 1] == '"') {
            const auto end = tag.find('"', quote + 2);

            if (end == std::string_view::npos) {
                break;
            }

            return tag.substr(quote + 2, end - quote - 2);
        }
    }

    return {};
}

std::array<std::uint8_t, 3> colour(std::string_view value) {
    const auto digit = [&](char c) {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }

        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }

        if (c >= 'A' && c <= 'F') {
            return c - 'A' + 10;
        }

        throw VoyemuError("UI_ARTWORK", "hexadecimal fill expected");
    };

    if (value.size() == 4 && value[0] == '#') {
        return {std::uint8_t(digit(value[1]) * 17), std::uint8_t(digit(value[2]) * 17),
                std::uint8_t(digit(value[3]) * 17)};
    }

    if (value.size() == 7 && value[0] == '#') {
        return {std::uint8_t(digit(value[1]) * 16 + digit(value[2])),
                std::uint8_t(digit(value[3]) * 16 + digit(value[4])),
                std::uint8_t(digit(value[5]) * 16 + digit(value[6]))};
    }

    throw VoyemuError("UI_ARTWORK", "hexadecimal fill expected");
}
} // namespace

VectorArtwork parse_vector_artwork(std::string_view svg) {
    VectorArtwork artwork;
    float left = INFINITY, top = INFINITY, right = -INFINITY, bottom = -INFINITY;

    for (auto at = svg.find("<path"); at != std::string_view::npos;
         at = svg.find("<path", at + 1)) {
        const auto end = svg.find('>', at);

        if (end == std::string_view::npos) {
            throw VoyemuError("UI_ARTWORK", "unterminated path element");
        }

        const auto tag = svg.substr(at + 1, end - at - 1);
        VectorArtwork::Shape shape;
        shape.contours = PathData(attribute(tag, "d")).contours();

        if (const auto fill = attribute(tag, "fill"); !fill.empty()) {
            shape.accent = true;
            artwork.accent = colour(fill);
        }

        for (const auto &contour : shape.contours) {
            for (const auto &[x, y] : contour) {
                left = std::min(left, x);
                top = std::min(top, y);
                right = std::max(right, x);
                bottom = std::max(bottom, y);
            }
        }

        artwork.shapes.push_back(std::move(shape));
    }

    if (artwork.shapes.empty() || !(right > left) || !(bottom > top)) {
        throw VoyemuError("UI_ARTWORK", "no filled paths");
    }

    artwork.x = left;
    artwork.y = top;
    artwork.width = right - left;
    artwork.height = bottom - top;

    return artwork;
}

std::vector<std::uint8_t> rasterize_artwork(const VectorArtwork &artwork, int width, int height) {
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
        throw VoyemuError("UI_ARTWORK", "raster size out of range");
    }

    constexpr int samples = 5;
    const int stride = width * 4;
    std::vector<std::uint8_t> pixels(std::size_t(stride) * (2 * height + artwork_layer_gap));
    const float sx = width / artwork.width, sy = height / artwork.height;

    for (const bool accent : {false, true}) {
        std::vector<float> coverage(std::size_t(width) * height);
        std::vector<float> crossings;

        for (const auto &shape : artwork.shapes) {
            if (shape.accent != accent) {
                continue;
            }

            std::vector<float> own(coverage.size());

            for (int row = 0; row < height; ++row) {
                for (int sample = 0; sample < samples; ++sample) {
                    const float y = row + (sample + .5f) / samples;
                    crossings.clear();

                    for (const auto &contour : shape.contours) {
                        for (std::size_t i = 0; i < contour.size(); ++i) {
                            const auto &a = contour[i], &b = contour[(i + 1) % contour.size()];
                            const float ay = (a[1] - artwork.y) * sy, by = (b[1] - artwork.y) * sy;

                            if ((ay <= y && y < by) || (by <= y && y < ay)) {
                                const float ax = (a[0] - artwork.x) * sx,
                                            bx = (b[0] - artwork.x) * sx;
                                crossings.push_back(ax + (y - ay) * (bx - ax) / (by - ay));
                            }
                        }
                    }

                    std::sort(crossings.begin(), crossings.end());

                    for (std::size_t i = 0; i + 1 < crossings.size(); i += 2) {
                        const float from = std::clamp(crossings[i], 0.f, float(width)),
                                    to = std::clamp(crossings[i + 1], 0.f, float(width));

                        for (int x = int(from); x < width && x < to; ++x) {
                            own[std::size_t(row) * width + x] +=
                                    (std::min(to, x + 1.f) - std::max(from, float(x))) / samples;
                        }
                    }
                }
            }

            for (std::size_t i = 0; i < coverage.size(); ++i) {
                coverage[i] = std::min(1.f, coverage[i] + own[i]);
            }
        }

        const int first = accent ? height + artwork_layer_gap : 0;

        for (int row = 0; row < height; ++row) {
            for (int x = 0; x < width; ++x) {
                auto *pixel = &pixels[std::size_t(first + row) * stride + std::size_t(x) * 4];
                pixel[0] = accent ? artwork.accent[0] : 255;
                pixel[1] = accent ? artwork.accent[1] : 255;
                pixel[2] = accent ? artwork.accent[2] : 255;
                pixel[3] = std::uint8_t(std::lround(
                        std::clamp(coverage[std::size_t(row) * width + x], 0.f, 1.f) * 255));
            }
        }
    }

    return pixels;
}
} // namespace voyemu::ui
