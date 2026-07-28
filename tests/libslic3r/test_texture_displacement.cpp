#define NOMINMAX
#include <catch2/catch_all.hpp>

#include <cmath>
#include <fstream>
#include <boost/filesystem.hpp>

#include "libslic3r/TextureDisplacement.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleSelector.hpp"
#include "libslic3r/PNGReadWrite.hpp"

using namespace Slic3r;
using Catch::Matchers::WithinAbs;

// Encodes a flat (uniform-value) grayscale image through Slic3r's own PNG writer/reader round
// trip, so decode_height_texture() (which only accepts true 8-bit grayscale PNG) is guaranteed a
// compatible file, exactly like the GUI's "Add texture" import path does.
static std::shared_ptr<std::vector<unsigned char>> make_flat_gray_png(uint8_t value, size_t w = 4, size_t h = 4)
{
    std::vector<uint8_t> pixels(w * h, value);
    const boost::filesystem::path tmp_path = boost::filesystem::temp_directory_path()
        / boost::filesystem::unique_path("texdisp_test_%%%%%%%%.png");
    REQUIRE(Slic3r::png::write_gray_to_file(tmp_path.string(), w, h, pixels));

    std::vector<unsigned char> bytes;
    {
        std::ifstream ifs(tmp_path.string(), std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
    }
    boost::system::error_code ec;
    boost::filesystem::remove(tmp_path, ec);

    REQUIRE_FALSE(bytes.empty());
    return std::make_shared<std::vector<unsigned char>>(std::move(bytes));
}

TEST_CASE("TextureDisplacement: decode_height_texture round-trips an 8-bit grayscale PNG", "[TextureDisplacement]")
{
    TextureDisplacementLayer layer;
    layer.image_data = make_flat_gray_png(128, 4, 4);

    DecodedHeightTexture tex = decode_height_texture(layer);
    REQUIRE_FALSE(tex.empty());
    CHECK(tex.width == 4);
    CHECK(tex.height == 4);
    REQUIRE_THAT(tex.sample(Vec2f(0.5f, 0.5f)), WithinAbs(128.0 / 255.0, 1.0 / 255.0));
}

TEST_CASE("TextureDisplacement: an empty layer list leaves the mesh unchanged", "[TextureDisplacement]")
{
    const indexed_triangle_set cube = its_make_cube(10., 10., 10.);
    const std::vector<TextureDisplacementLayer> layers; // none
    TextureDisplacementFacetsData facets{};              // all empty

    const indexed_triangle_set result = build_texture_displacement(cube, layers, facets);

    REQUIRE(result.vertices.size() == cube.vertices.size());
    REQUIRE(result.indices.size() == cube.indices.size());
    for (size_t i = 0; i < cube.vertices.size(); ++i)
        for (int c = 0; c < 3; ++c)
            CHECK(result.vertices[i](c) == cube.vertices[i](c));
}

TEST_CASE("TextureDisplacement: fully painting a mesh displaces every vertex along its own normal", "[TextureDisplacement]")
{
    const indexed_triangle_set cube = its_make_cube(10., 10., 10.);
    const TriangleMesh cube_mesh(cube);

    TriangleSelector selector(cube_mesh);
    for (int f = 0; f < int(cube.indices.size()); ++f)
        selector.set_facet(f, EnforcerBlockerType::ENFORCER);

    TextureDisplacementFacetsData facets{};
    facets[0] = selector.serialize();

    TextureDisplacementLayer layer;
    layer.slot        = 0;
    layer.depth_mm    = 2.0f;
    layer.tiling_scale = 5.0f;
    layer.image_data  = make_flat_gray_png(255); // sample() == 1.0 everywhere -> full depth_mm displacement

    const indexed_triangle_set result = build_texture_displacement(cube, {layer}, facets);

    REQUIRE(result.vertices.size() == cube.vertices.size());
    for (size_t i = 0; i < cube.vertices.size(); ++i) {
        const float moved = (result.vertices[i] - cube.vertices[i]).norm();
        CHECK_THAT(moved, WithinAbs(layer.depth_mm, 1e-3f));
    }
}

// Paints every facet of `mesh` into a serialized mask, the way "Select whole model" does.
static TriangleSelector::TriangleSplittingData paint_whole_mesh(const indexed_triangle_set &mesh)
{
    const TriangleMesh tm(mesh);
    TriangleSelector   selector(tm);
    for (int f = 0; f < int(mesh.indices.size()); ++f)
        selector.set_facet(f, EnforcerBlockerType::ENFORCER);
    return selector.serialize();
}

// Regression test for the bug this feature shipped with: with two layers painted over the same
// area, the second one was silently dropped (its paint mask was remapped onto the mesh the first
// layer had already displaced, which routinely produced an empty bitstream). Every layer is now
// evaluated against the original mesh instead, so both must show up in the total.
TEST_CASE("TextureDisplacement: a second layer over the same area is applied too", "[TextureDisplacement]")
{
    const indexed_triangle_set cube = its_make_cube(10., 10., 10.);

    TextureDisplacementFacetsData facets{};
    facets[0] = paint_whole_mesh(cube);
    facets[1] = facets[0]; // both layers cover the whole cube

    TextureDisplacementLayer base;
    base.slot         = 0;
    base.depth_mm     = 1.0f;
    base.tiling_scale = 5.0f;
    base.image_data   = make_flat_gray_png(255); // height 1.0 everywhere

    TextureDisplacementLayer second = base;
    second.slot       = 1;
    second.depth_mm   = 0.5f;
    second.blend_mode = TextureBlendMode::Add;

    const indexed_triangle_set result = build_texture_displacement(cube, {base, second}, facets);

    // Topology is preserved exactly, so vertices can be compared 1:1 with the input.
    REQUIRE(result.vertices.size() == cube.vertices.size());
    REQUIRE(result.indices.size() == cube.indices.size());
    for (size_t i = 0; i < cube.vertices.size(); ++i)
        CHECK_THAT((result.vertices[i] - cube.vertices[i]).norm(), WithinAbs(1.5f, 1e-3f)); // 1.0 + 0.5, not just 1.0
}

TEST_CASE("TextureDisplacement: blend modes combine a layer with the ones below it", "[TextureDisplacement]")
{
    const indexed_triangle_set cube = its_make_cube(10., 10., 10.);

    TextureDisplacementFacetsData facets{};
    facets[0] = paint_whole_mesh(cube);
    facets[1] = facets[0];

    TextureDisplacementLayer base;
    base.slot         = 0;
    base.depth_mm     = 2.0f;
    base.tiling_scale = 5.0f;
    base.image_data   = make_flat_gray_png(255); // -> contributes exactly +2.0 mm

    TextureDisplacementLayer second = base;
    second.slot     = 1;
    second.depth_mm = 0.5f; // -> its own value is 0.5 mm

    // Expected total displacement for each mode, given base = 2.0 mm and second = 0.5 mm. Multiply
    // and Divide treat the layer's value as a factor relative to 1 mm (see TextureBlendMode).
    const auto expected = GENERATE(table<TextureBlendMode, float>({
        { TextureBlendMode::Add,      2.5f },  // 2.0 + 0.5
        { TextureBlendMode::Subtract, 1.5f },  // 2.0 - 0.5
        { TextureBlendMode::Multiply, 1.0f },  // 2.0 * 0.5
        { TextureBlendMode::Divide,   4.0f },  // 2.0 / 0.5
    }));
    second.blend_mode = std::get<0>(expected);

    const indexed_triangle_set result = build_texture_displacement(cube, {base, second}, facets);

    REQUIRE(result.vertices.size() == cube.vertices.size());
    for (size_t i = 0; i < cube.vertices.size(); ++i)
        CHECK_THAT((result.vertices[i] - cube.vertices[i]).norm(), WithinAbs(std::get<1>(expected), 1e-3f));
}

TEST_CASE("TextureDisplacement: the lowest layer ignores its blend mode", "[TextureDisplacement]")
{
    // Multiply against the implicit zero base would annihilate the only layer present; the first
    // layer to reach a vertex always starts the total off additively instead.
    const indexed_triangle_set cube = its_make_cube(10., 10., 10.);

    TextureDisplacementFacetsData facets{};
    facets[0] = paint_whole_mesh(cube);

    TextureDisplacementLayer layer;
    layer.slot         = 0;
    layer.depth_mm     = 2.0f;
    layer.tiling_scale = 5.0f;
    layer.blend_mode   = TextureBlendMode::Multiply;
    layer.image_data   = make_flat_gray_png(255);

    const indexed_triangle_set result = build_texture_displacement(cube, {layer}, facets);

    for (size_t i = 0; i < cube.vertices.size(); ++i)
        CHECK_THAT((result.vertices[i] - cube.vertices[i]).norm(), WithinAbs(2.0f, 1e-3f));
}

TEST_CASE("TextureDisplacement: boundary vertices shared with unpainted triangles are pinned", "[TextureDisplacement]")
{
    // A small triangle fan around a central vertex O, with 4 outer points A/B/C/D forming 4
    // triangles T0..T3 in the XY plane. Only T0, T1, T2 are painted, T3 is left unpainted:
    //   O: touches all 4 triangles (incl. unpainted T3)      -> boundary, must NOT move
    //   A: touches T0 (painted) and T3 (unpainted)           -> boundary, must NOT move
    //   D: touches T2 (painted) and T3 (unpainted)           -> boundary, must NOT move
    //   B: touches only T0 and T1 (both painted)              -> interior, SHOULD move
    //   C: touches only T1 and T2 (both painted)              -> interior, SHOULD move
    indexed_triangle_set fan;
    fan.vertices = { {0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {-1.f, 0.f, 0.f}, {0.f, -1.f, 0.f} };
    fan.indices  = { {0, 1, 2}, {0, 2, 3}, {0, 3, 4}, {0, 4, 1} };

    const TriangleMesh fan_mesh(fan);
    TriangleSelector    selector(fan_mesh);
    selector.set_facet(0, EnforcerBlockerType::ENFORCER);
    selector.set_facet(1, EnforcerBlockerType::ENFORCER);
    selector.set_facet(2, EnforcerBlockerType::ENFORCER);
    // facet 3 (T3) is left at its default EnforcerBlockerType::NONE.

    TextureDisplacementFacetsData facets{};
    facets[0] = selector.serialize();

    TextureDisplacementLayer layer;
    layer.slot        = 0;
    layer.depth_mm    = 1.0f;
    layer.tiling_scale = 5.0f;
    layer.image_data  = make_flat_gray_png(255);

    const indexed_triangle_set result = build_texture_displacement(fan, {layer}, facets);

    // Find each named vertex's post-bake position by matching the original (pinned vertices keep
    // their exact original position; moved ones won't match any original position anymore).
    auto still_at_original_position = [&](const Vec3f &original) {
        for (const Vec3f &v : result.vertices)
            if ((v - original).norm() < 1e-6f)
                return true;
        return false;
    };

    CHECK(still_at_original_position(fan.vertices[0])); // O: boundary
    CHECK(still_at_original_position(fan.vertices[1])); // A: boundary
    CHECK(still_at_original_position(fan.vertices[4])); // D: boundary
    CHECK_FALSE(still_at_original_position(fan.vertices[2])); // B: interior, must have moved
    CHECK_FALSE(still_at_original_position(fan.vertices[3])); // C: interior, must have moved
}

// Every undirected edge of a closed manifold mesh is shared by exactly two triangles. A T-junction
// (a hanging node where a refined region meets a coarse one) breaks that: the coarse side spans an
// edge that the fine side has replaced with two half-edges, so those three edges each show up an
// odd number of times. Counting edge uses is therefore an exact crack detector for a closed mesh.
static bool every_edge_used_twice(const indexed_triangle_set &its)
{
    std::map<std::pair<int, int>, int> uses;
    for (const auto &t : its.indices)
        for (int e = 0; e < 3; ++e) {
            int a = t[e], b = t[(e + 1) % 3];
            if (a > b)
                std::swap(a, b);
            ++uses[{ a, b }];
        }
    for (const auto &[edge, n] : uses)
        if (n != 2)
            return false;
    return true;
}

TEST_CASE("TextureDisplacement: adaptive subdivision is conformal and region-restricted", "[TextureDisplacement]")
{
    const indexed_triangle_set cube = its_make_cube(10., 10., 10.);
    REQUIRE(every_edge_used_twice(cube)); // sanity: the input really is a closed manifold

    auto longest_edge = [](const indexed_triangle_set &its, const stl_triangle_vertex_indices &t) {
        float m = 0.f;
        for (int e = 0; e < 3; ++e)
            m = std::max(m, (its.vertices[t[e]] - its.vertices[t[(e + 1) % 3]]).norm());
        return m;
    };

    SECTION("whole-mesh region refines everywhere and stays conformal")
    {
        std::vector<uint8_t> region(cube.indices.size(), 1);
        std::vector<int>     source;
        const indexed_triangle_set out = subdivide_mesh_adaptive(cube, region, 3.f, 100000, &source);

        CHECK(out.indices.size() > cube.indices.size()); // it actually refined
        CHECK(every_edge_used_twice(out));               // ... without opening a single crack

        // Refinement runs to completion, not for a fixed number of passes: with the whole mesh in the
        // region and budget to spare, *every* edge really does end up at or below the target. This is
        // the regression that matters - an earlier version quietly stopped a long way short, having
        // spent its pass budget grading the coarse surroundings.
        float worst = 0.f;
        for (const auto &t : out.indices)
            worst = std::max(worst, longest_edge(out, t));
        CHECK(worst <= 3.f);

        REQUIRE(source.size() == out.indices.size());
        for (int s : source)
            CHECK((s >= 0 && s < int(cube.indices.size()))); // every child names a real parent
    }

    SECTION("a partial region refines only there, and the boundary is still crack-free")
    {
        // Refine only the triangles whose centroid is in the upper (z > 5) half of the cube.
        std::vector<uint8_t> region(cube.indices.size(), 0);
        size_t               region_count = 0;
        for (size_t i = 0; i < cube.indices.size(); ++i) {
            const auto &t = cube.indices[i];
            const float cz = (cube.vertices[t[0]].z() + cube.vertices[t[1]].z() + cube.vertices[t[2]].z()) / 3.f;
            if (cz > 5.f) {
                region[i] = 1;
                ++region_count;
            }
        }
        REQUIRE(region_count > 0);

        std::vector<int>           source;
        const indexed_triangle_set out = subdivide_mesh_adaptive(cube, region, 2.f, 100000, &source);

        CHECK(out.indices.size() > cube.indices.size());
        CHECK(every_edge_used_twice(out)); // the refined/coarse seam has no T-junction

        // Inside the region the target is actually met - refinement is not cut short by a pass budget.
        // Outside it, only the graded transition band conformality requires is touched, so plenty of
        // the unpainted mesh is still coarser than the target: the region was not a suggestion.
        float max_in = 0.f, max_out = 0.f;
        for (size_t i = 0; i < out.indices.size(); ++i) {
            float &acc = region[source[i]] ? max_in : max_out;
            acc        = std::max(acc, longest_edge(out, out.indices[i]));
        }
        CHECK(max_in <= 2.f);
        CHECK(max_out > 2.f);

        std::vector<uint8_t>       all(cube.indices.size(), 1);
        const indexed_triangle_set whole = subdivide_mesh_adaptive(cube, all, 2.f, 100000);
        CHECK(out.indices.size() < whole.indices.size()); // ... and it cost less than doing the lot
    }

    SECTION("the triangle budget caps the result and still leaves a conformal mesh")
    {
        std::vector<uint8_t>       region(cube.indices.size(), 1);
        const indexed_triangle_set out = subdivide_mesh_adaptive(cube, region, 0.05f, /*max_triangles*/ 500);
        CHECK(out.indices.size() <= 500);
        CHECK(out.indices.size() > cube.indices.size()); // it spent the budget rather than giving up
        CHECK(every_edge_used_twice(out));               // stopping on the budget is not a crack
    }

    SECTION("an empty region is a no-op")
    {
        std::vector<uint8_t>       region(cube.indices.size(), 0);
        const indexed_triangle_set out = subdivide_mesh_adaptive(cube, region, 1.f, 100000, nullptr);
        CHECK(out.indices.size() == cube.indices.size());
        CHECK(out.vertices.size() == cube.vertices.size());
    }
}

TEST_CASE("TextureDisplacement: feature-adaptive subdivision follows curvature, not slope", "[TextureDisplacement]")
{
    // A flat sheet, tessellated into a regular grid to give the bisector something to work with.
    indexed_triangle_set plane;
    plane.vertices = { { 0.f, 0.f, 0.f }, { 1.f, 0.f, 0.f }, { 1.f, 1.f, 0.f }, { 0.f, 1.f, 0.f } };
    plane.indices  = { { 0, 1, 2 }, { 0, 2, 3 } };
    const indexed_triangle_set grid = subdivide_mesh_uniform(plane, 0.15f, 5); // ~uniform grid of small triangles
    REQUIRE(grid.indices.size() > 32);

    const std::vector<uint8_t> region(grid.indices.size(), 1);

    auto longest_edge = [](const indexed_triangle_set &its, const stl_triangle_vertex_indices &t) {
        float m = 0.f;
        for (int e = 0; e < 3; ++e)
            m = std::max(m, (its.vertices[t[e]] - its.vertices[t[(e + 1) % 3]]).norm());
        return m;
    };
    auto centroid_xy = [](const indexed_triangle_set &its, const stl_triangle_vertex_indices &t) {
        return Vec2f((its.vertices[t[0]].x() + its.vertices[t[1]].x() + its.vertices[t[2]].x()) / 3.f,
                     (its.vertices[t[0]].y() + its.vertices[t[1]].y() + its.vertices[t[2]].y()) / 3.f);
    };

    SECTION("a sharp bump refines densely at its center and leaves flat corners coarse")
    {
        // A tight Gaussian bump at the sheet's center: strong curvature near (0.5, 0.5), flat far away.
        HeightFieldSampler bump = [](const Vec3f &p, const Vec3f &) {
            const float r2 = (p.x() - 0.5f) * (p.x() - 0.5f) + (p.y() - 0.5f) * (p.y() - 0.5f);
            return 1.0f * std::exp(-r2 / 0.02f);
        };

        // Baseline max edge 0.3 is coarser than the grid's own edges, so the baseline adds nothing
        // here - this isolates the *curvature* contribution (the grid already meets the baseline).
        std::vector<int>           source;
        const indexed_triangle_set out =
            subdivide_mesh_adaptive(grid, region, /*max edge*/ 0.3f, 200000, &source, bump, /*tol*/ 0.02f,
                                    /*min_edge*/ 0.01f);

        CHECK(out.indices.size() > grid.indices.size()); // the bump forced real refinement

        // The largest triangle near the bump's center must be much smaller than the largest in a flat
        // corner - i.e. triangles went where the curvature is, not spread evenly.
        float near_max = 0.f, far_max = 0.f;
        for (const auto &t : out.indices) {
            const Vec2f c   = centroid_xy(out, t);
            const float r   = (c - Vec2f(0.5f, 0.5f)).norm();
            const float len = longest_edge(out, t);
            if (r < 0.1f)
                near_max = std::max(near_max, len);
            else if (r > 0.45f)
                far_max = std::max(far_max, len);
        }
        REQUIRE(near_max > 0.f);
        REQUIRE(far_max > 0.f);
        CHECK(near_max < far_max); // finer at the hill than on the flats
    }

    SECTION("a linear ramp has zero curvature and is left untouched")
    {
        // Height varies, but linearly - a flat triangle represents it exactly, so the chord error is
        // zero everywhere and nothing should be split. This is the case a gradient-based criterion
        // would wrongly over-refine.
        HeightFieldSampler ramp = [](const Vec3f &p, const Vec3f &) { return 2.0f * p.x(); };

        // Same coarse baseline (0.3) that the grid already meets, so any split would be curvature-
        // driven - and a ramp has none.
        const indexed_triangle_set out =
            subdivide_mesh_adaptive(grid, region, /*max edge*/ 0.3f, 200000, nullptr, ramp, /*tol*/ 0.02f,
                                    /*min_edge*/ 0.01f);

        CHECK(out.indices.size() == grid.indices.size()); // not one extra triangle
    }

    SECTION("the max-edge baseline still applies in feature mode")
    {
        // A height field that is flat everywhere the four sample points of a coarse triangle happen to
        // land, but not in between - the aliasing case where a chord test alone reports no error and
        // refinement stalls before it ever starts. The baseline is what stops that: it guarantees a
        // sampling density fine enough for the curvature test to see the texture at all.
        HeightFieldSampler flat = [](const Vec3f &, const Vec3f &) { return 0.f; };

        const indexed_triangle_set out =
            subdivide_mesh_adaptive(grid, region, /*max edge*/ 0.03f, 200000, nullptr, flat, /*tol*/ 0.02f,
                                    /*min_edge*/ 0.001f);

        CHECK(out.indices.size() > grid.indices.size());
        float worst = 0.f;
        for (const auto &t : out.indices)
            worst = std::max(worst, longest_edge(out, t));
        CHECK(worst <= 0.03f);
    }
}
