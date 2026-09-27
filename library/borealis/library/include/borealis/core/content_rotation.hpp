#pragma once

namespace brls {
// Inverse of the clockwise NanoVG transform, in windowScale units.
template <typename PointType>
PointType inverseContentRotation(PointType p, float width, float height, unsigned turns) {
    switch (turns % 4) {
        case 1: return PointType(p.y, width - p.x);
        case 2: return PointType(width - p.x, height - p.y);
        case 3: return PointType(height - p.y, p.x);
        default: return p;
    }
}
}
