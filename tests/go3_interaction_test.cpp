#include <borealis/core/content_rotation.hpp>
#include "utils/edge_back.hpp"
#include "utils/pull_refresh.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
struct Point { float x, y; Point(float x, float y):x(x),y(y) {} };
int main() {
    // Round-trip every rotation at landscape/portrait sizes and non-integral DPI scales.
    for (float scale : {0.75f, 1.0f, 1.5f, 2.0f})
    for (Point pixels : {Point(1920,1080), Point(1080,1920), Point(1280,853)})
    for (unsigned rotation=0; rotation<4; ++rotation) {
        float w=pixels.x/scale, h=pixels.y/scale;
        float logicalW=(rotation%2)?h:w, logicalH=(rotation%2)?w:h;
        for (Point p : {Point(0,0),Point(logicalW,logicalH),Point(logicalW*.23f,logicalH*.61f)}) {
            Point screen=p;
            if(rotation==1) screen=Point(w-p.y,p.x);
            if(rotation==2) screen=Point(w-p.x,h-p.y);
            if(rotation==3) screen=Point(p.y,h-p.x);
            Point back=brls::inverseContentRotation(screen,w,h,rotation);
            assert(std::abs(back.x-p.x)<0.001f && std::abs(back.y-p.y)<0.001f);
        }
    }
    PullRefreshState pull;
    pull.begin(true); pull.move(143); assert(!pull.release()); // below threshold
    pull.begin(true); pull.move(144); assert(pull.release()); assert(!pull.release()); // exactly once
    pull.begin(true); pull.move(240); assert(pull.armed()); pull.move(90); assert(!pull.release()); // reverse to cancel
    pull.begin(false); pull.move(1000); assert(!pull.release()); // mid-list / loading
    pull.begin(true); pull.move(180); pull.cancel(); assert(!pull.release()); // interruption
    pull.begin(true); pull.move(-200); assert(pull.distance==0 && !pull.release());
    pull.begin(true); pull.move(10000); assert(pull.distance==112); pull.release(); // resistance cap

    // Edge back: only a drag starting at a screen edge and moving inwards fires.
    EdgeBackState edge;
    edge.begin(10, 1280, 24); assert(edge.active && edge.fromLeft);
    edge.move(89); assert(!edge.release()); // 79px: below threshold
    edge.begin(10, 1280, 24); edge.move(90); assert(edge.release()); assert(!edge.release()); // 80px, once
    edge.begin(1270, 1280, 24); assert(edge.active && !edge.fromLeft);
    edge.move(1191); assert(!edge.armed()); edge.move(1190); assert(edge.release()); // right edge
    edge.begin(640, 1280, 24); assert(!edge.active); edge.move(900); assert(!edge.release()); // not an edge
    edge.begin(10, 1280, 24); edge.move(-40); assert(edge.distance < 0 && !edge.release()); // moved outwards
    edge.begin(10, 1280, 24); edge.move(200); edge.cancel(); assert(!edge.release()); // interrupted

    std::cout << "PASS: 144 rotation round trips, pull refresh and edge back threshold/cancel cases\n";
}
