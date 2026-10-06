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
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <optional>
#include <random>
#include <vector>

#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/CAD/SketchEngine.hpp"
#include "libslic3r/Point.hpp"
#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/CAD/DesignSketchTool.hpp"

using namespace Slic3r;
using namespace Slic3r::GUI;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

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

// The Design tab's base planes, all through one origin, as DesignPanel shows them.
std::vector<SketchPlane> base_planes() { return { SketchPlane::XY(), SketchPlane::XZ(), SketchPlane::YZ() }; }

struct View
{
    Vec3d eye;
    Vec3d forward;
    bool  perspective;
};

// Eyes in four octants, off every plane, plus two orthographic directions.
const View kViews[] = {
    { Vec3d(300., -400., 250.), Vec3d(-300., 400., -250.).normalized(), true },
    { Vec3d(-350., -200., 300.), Vec3d(350., 200., -300.).normalized(), true },
    { Vec3d(250., 300., -200.), Vec3d(-250., -300., 200.).normalized(), true },
    { Vec3d(-300., 350., -250.), Vec3d(300., -350., 250.).normalized(), true },
    { Vec3d::Zero(), Vec3d(-0.5, 0.7, -0.5).normalized(), false },
    { Vec3d::Zero(), Vec3d(0.3, 0.4, 0.85).normalized(), false },
};

double area(const PlanePiece& piece, const SketchPlane& plane)
{
    Vec3d sum = Vec3d::Zero();
    for (size_t i = 0; i < piece.corners.size(); ++i)
        sum += piece.corners[i].cross(piece.corners[(i + 1) % piece.corners.size()]);
    return 0.5 * std::abs(sum.dot(plane.x_axis.cross(plane.y_axis)));
}

// How far along the ray (from, unit dir) it crosses `piece`, or nothing if it misses.
std::optional<double> hit_distance(const PlanePiece& piece, const SketchPlane& plane, const Vec3d& from, const Vec3d& dir)
{
    const Vec3d  n  = plane.x_axis.cross(plane.y_axis);
    const double dn = n.dot(dir);
    if (std::abs(dn) < 1e-9)
        return std::nullopt;
    const double t = n.dot(piece.corners.front() - from) / dn;
    if (t <= 0.)
        return std::nullopt;
    const Vec3d x  = from + dir * t;
    double      lo = 0., hi = 0.;
    for (size_t i = 0; i < piece.corners.size(); ++i) {
        const Vec3d& a = piece.corners[i];
        const Vec3d& b = piece.corners[(i + 1) % piece.corners.size()];
        const double s = (b - a).cross(x - a).dot(n);
        lo = std::min(lo, s);
        hi = std::max(hi, s);
    }
    if (lo < 0. && hi > 0.)
        return std::nullopt;   // outside one of the edges
    return t;
}

// Whether x lies on one of the segments the pieces are outlined with.
bool outlined(const std::vector<PlanePiece>& pieces, const Vec3d& x)
{
    for (const PlanePiece& piece : pieces)
        for (const auto& [a, b] : piece.lines) {
            const Vec3d  ab = b - a;
            const double t  = std::clamp((x - a).dot(ab) / ab.squaredNorm(), 0., 1.);
            if ((a + ab * t - x).norm() < 1e-6)
                return true;
        }
    return false;
}

std::vector<PlanePiece> pieces_of(const std::vector<SketchPlane>& planes)
{
    const View& view = kViews[0];
    return planes_back_to_front(planes, 75., view.eye, view.forward, view.perspective);
}

struct SightLines
{
    int overlapping  = 0;   // sight lines through two or more pieces, where draw order matters
    int out_of_order = 0;   // ...of which meet a nearer piece before a farther one
};

// Translucent pieces blend correctly only if, along every line of sight, each piece is drawn after
// every piece behind it.
SightLines sight_lines(const std::vector<SketchPlane>& planes, double half, const View& view)
{
    const std::vector<PlanePiece> pieces = planes_back_to_front(planes, half, view.eye, view.forward, view.perspective);
    std::mt19937                           rng(7);
    std::uniform_real_distribution<double> coord(-0.95 * half, 0.95 * half);
    SightLines                             seen;
    for (int r = 0; r < 500; ++r) {
        const Vec3d target(coord(rng), coord(rng), coord(rng));
        const Vec3d from = view.perspective ? view.eye : Vec3d(target - view.forward * (10. * half));
        const Vec3d dir  = (target - from).normalized();
        double      last = std::numeric_limits<double>::max();
        int         hits = 0;
        bool        ok   = true;
        for (const PlanePiece& piece : pieces)
            if (const std::optional<double> t = hit_distance(piece, planes[piece.plane], from, dir)) {
                ok   = ok && *t <= last + 1e-6 * half;
                last = *t;
                ++hits;
            }
        if (hits >= 2) {
            ++seen.overlapping;
            seen.out_of_order += ok ? 0 : 1;
        }
    }
    return seen;
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

TEST_CASE("Crossing base planes are drawn back to front from any viewpoint", "[DesignSketchTool]")
{
    const View&      view = kViews[GENERATE(range(0, int(std::size(kViews))))];
    const SightLines seen = sight_lines(base_planes(), 75., view);
    CHECK(seen.overlapping >= 200);   // of the 500: most lines of sight into the planes cross two
    CHECK(seen.out_of_order == 0);
}

TEST_CASE("Datum planes are drawn back to front among the base planes", "[DesignSketchTool]")
{
    // A datum parallel to XY 30 mm up, and one tilted 30 degrees about X through (0, 0, 10).
    std::vector<SketchPlane> planes = base_planes();
    SketchPlane              raised = SketchPlane::XY();
    raised.origin                   = Vec3d(0., 0., 30.);
    SketchPlane tilted;
    tilted.origin = Vec3d(0., 0., 10.);
    tilted.y_axis = Vec3d(0., std::cos(M_PI / 6.), std::sin(M_PI / 6.));
    tilted.normal = tilted.x_axis.cross(tilted.y_axis);
    planes.push_back(raised);
    planes.push_back(tilted);

    const View&      view = kViews[GENERATE(range(0, int(std::size(kViews))))];
    const SightLines seen = sight_lines(planes, 75., view);
    CHECK(seen.overlapping >= 200);
    CHECK(seen.out_of_order == 0);
}

TEST_CASE("Base planes are outlined along every line where they cross", "[DesignSketchTool]")
{
    // XY, XZ and YZ cross along the three axes, all through the middle of each 75 mm half-square.
    const std::vector<PlanePiece> pieces = pieces_of(base_planes());
    int                           missed = 0;
    for (double t = -70.; t <= 70.; t += 10.)
        for (const Vec3d& axis : { Vec3d(1., 0., 0.), Vec3d(0., 1., 0.), Vec3d(0., 0., 1.) })
            missed += outlined(pieces, axis * t) ? 0 : 1;
    CHECK(missed == 0);
}

TEST_CASE("A datum clear of the base planes gets no lines across it", "[DesignSketchTool]")
{
    // Parallel to YZ at x = 100, past the 75 mm half-squares of XY and XZ: the infinite XY and XZ
    // planes still cut it, along z = 0 and y = 0, but the squares never meet.
    std::vector<SketchPlane> planes = base_planes();
    SketchPlane              beyond = SketchPlane::YZ();
    beyond.origin                   = Vec3d(100., 0., 0.);
    planes.push_back(beyond);

    const std::vector<PlanePiece> pieces = pieces_of(planes);
    CHECK_FALSE(outlined(pieces, Vec3d(100., 30., 0.)));
    CHECK_FALSE(outlined(pieces, Vec3d(100., 0., 30.)));
}

TEST_CASE("A line where two squares cross stops where either square ends", "[DesignSketchTool]")
{
    // Parallel to XY 30 mm up and moved 60 mm along X, so it spans x = -15..135. It meets XZ along
    // y = 0, z = 30, but XZ's square only reaches x = 75.
    std::vector<SketchPlane> planes = base_planes();
    SketchPlane              raised = SketchPlane::XY();
    raised.origin                   = Vec3d(60., 0., 30.);
    planes.push_back(raised);

    const std::vector<PlanePiece> pieces = pieces_of(planes);
    CHECK(outlined(pieces, Vec3d(0., 0., 30.)));
    CHECK(outlined(pieces, Vec3d(70., 0., 30.)));
    CHECK_FALSE(outlined(pieces, Vec3d(100., 0., 30.)));
}

TEST_CASE("Cutting the base planes along each other keeps every plane whole", "[DesignSketchTool]")
{
    const std::vector<SketchPlane> planes = base_planes();
    const double                   half   = 75.;
    const View&                    view   = kViews[0];
    std::vector<double>            covered(planes.size(), 0.);
    for (const PlanePiece& piece : planes_back_to_front(planes, half, view.eye, view.forward, view.perspective))
        covered[piece.plane] += area(piece, planes[piece.plane]);
    for (double a : covered)
        CHECK_THAT(a, WithinRel(4. * half * half, 1e-9));
}
