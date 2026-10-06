// Orca: This suite links libslic3r_gui; the Design tab's sketch tool needs no wx application or GL
// context until it renders.
#ifdef WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
    // Match the GUI precompiled header: wx/msw/wrapcctl.h needs HDITEM from CommCtrl.h.
    #include <CommCtrl.h>
#endif

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/CAD/SketchEngine.hpp"
#include "libslic3r/Point.hpp"
#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/CAD/DesignSketchTool.hpp"

using namespace Slic3r;
using namespace Slic3r::GUI;
using Catch::Matchers::WithinAbs;

namespace {

SketchEntity circle(const Vec2d& c, double r)
{
    SketchEntity e;
    e.type   = SketchEntity::Type::Circle;
    e.center = e.p0 = c;
    e.radius = r;
    return e;
}

// Two committed sketches on XY: feature 1 a 10 mm circle at (50, 20), feature 2 a 5 mm one at (-60, 0).
void show_two_sketches(DesignSketchTool& tool)
{
    tool.set_display_sketches({ { { circle({ 50., 20. }, 10.) }, SketchPlane::XY(), 1 },
                                { { circle({ -60., 0. }, 5.) }, SketchPlane::XY(), 2 } });
}

} // namespace

TEST_CASE("Fit frames the picked sketch region, not the other sketches", "[DesignSketchTool]")
{
    DesignSketchTool   tool;
    GLVolumeCollection no_bodies;
    show_two_sketches(tool);
    tool.set_display_pick(1, 0);

    const BoundingBoxf3 box = tool.fit_box(no_bodies);
    REQUIRE(box.defined);
    CHECK_THAT(box.min.x(), WithinAbs(40., 0.5));
    CHECK_THAT(box.max.x(), WithinAbs(60., 0.5));
    CHECK_THAT(box.min.y(), WithinAbs(10., 0.5));
    CHECK_THAT(box.max.y(), WithinAbs(30., 0.5));
}

TEST_CASE("Fit frames every sketch when nothing is picked", "[DesignSketchTool]")
{
    DesignSketchTool   tool;
    GLVolumeCollection no_bodies;
    show_two_sketches(tool);

    const BoundingBoxf3 box = tool.fit_box(no_bodies);
    REQUIRE(box.defined);
    CHECK_THAT(box.min.x(), WithinAbs(-65., 0.5));
    CHECK_THAT(box.max.x(), WithinAbs(60., 0.5));
}

TEST_CASE("Fit frames the sketch being drawn along with the committed ones", "[DesignSketchTool]")
{
    DesignSketchTool   tool;
    GLVolumeCollection no_bodies;
    show_two_sketches(tool);
    // A 100 mm line up the XZ plane, whose y axis is world Z.
    SketchEntity line;
    line.p0 = { 0., 0. };
    line.p1 = { 0., 100. };
    tool.begin_edit({ line }, {}, SketchPlane::XZ());

    const BoundingBoxf3 box = tool.fit_box(no_bodies);
    REQUIRE(box.defined);
    CHECK_THAT(box.max.z(), WithinAbs(100., 1e-6));
    CHECK_THAT(box.min.x(), WithinAbs(-65., 0.5));
}

TEST_CASE("Fit gives a flat sketch depth, so it can be framed edge-on", "[DesignSketchTool]")
{
    DesignSketchTool   tool;
    GLVolumeCollection no_bodies;
    show_two_sketches(tool);
    tool.set_display_pick(2, 0);

    const BoundingBoxf3 box = tool.fit_box(no_bodies);
    REQUIRE(box.defined);
    CHECK(box.size().z() > 0.);
    CHECK_THAT(box.center().z(), WithinAbs(0., 1e-9));
}

TEST_CASE("Fit frames nothing when the Design tab shows nothing", "[DesignSketchTool]")
{
    DesignSketchTool   tool;
    GLVolumeCollection no_bodies;
    CHECK_FALSE(tool.fit_box(no_bodies).defined);
}
