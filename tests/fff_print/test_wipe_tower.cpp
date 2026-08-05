#include <catch2/catch_all.hpp>

#include <string>

#include "libslic3r/GCode/WipeTower.hpp"
#include "libslic3r/PrintConfig.hpp"

#include "test_helpers.hpp"

using namespace Slic3r;
using namespace Slic3r::Test;

// Pins the enum's size: the two GENERATE lists below hand-list every non-Klipper flavor, so a
// 14th `GCodeFlavor` value would silently go untested unless this fails the build first.
static_assert(int(gcfNoExtrusion) == 12, "GCodeFlavor grew: add the new value to the GENERATE lists in this file");

// The wipe tower flushes the firmware's motion queue around an M104/M109 or custom-G-code
// boundary. Klipper acts on those the moment it parses them, and its G4 reads only P, so the
// zero dwell every other flavor uses is not a flush there.
TEST_CASE("Klipper flushes the wipe tower planner queue with M400", "[WipeTower]")
{
    CHECK(std::string(flush_planner_queue_command(gcfKlipper)) == "M400\n");
}

TEST_CASE("Other flavors flush the wipe tower planner queue with a zero dwell", "[WipeTower]")
{
    const GCodeFlavor flavor = GENERATE(gcfMarlinLegacy, gcfRepRapFirmware, gcfRepetier,
                                        gcfMarlinFirmware, gcfRepRapSprinter, gcfTeacup,
                                        gcfMakerWare, gcfSailfish, gcfMach3, gcfMachinekit,
                                        gcfSmoothie, gcfNoExtrusion);
    INFO("gcode flavor enum value: " << int(flavor));
    CHECK(std::string(flush_planner_queue_command(flavor)) == "G4 S0\n");
}

// A timed pause is emitted in seconds for most firmware. Klipper's G4 reads only P, in
// milliseconds, and ignores S, so the seconds form would pause for no time at all there.
// 1.5s is exactly representable as a float, so neither form can drift when rounded.
TEST_CASE("Klipper waits in the wipe tower with a millisecond dwell", "[WipeTower]")
{
    CHECK(wait_command(gcfKlipper, 1.5f) == "G4 P1500\n");
}

TEST_CASE("Other flavors wait in the wipe tower with a seconds dwell", "[WipeTower]")
{
    const GCodeFlavor flavor = GENERATE(gcfMarlinLegacy, gcfRepRapFirmware, gcfRepetier,
                                        gcfMarlinFirmware, gcfRepRapSprinter, gcfTeacup,
                                        gcfMakerWare, gcfSailfish, gcfMach3, gcfMachinekit,
                                        gcfSmoothie, gcfNoExtrusion);
    INFO("gcode flavor enum value: " << int(flavor));
    CHECK(wait_command(flavor, 1.5f) == "G4 S1.500\n");
}

// The two helpers above are only unit-tested in isolation. Nothing yet confirms that a
// Klipper `gcode_flavor` actually reaches the wipe tower writer and lands in the exported
// G-code, which is the binding constraint of both changes above ("only gcfKlipper changes").
// These slice a real two-filament print and check that.

// The G-code between each "WIPE_TOWER_START"/"WIPE_TOWER_END" tag pair the wipe tower writes
// around its toolchange chunks, concatenated. Isolates the region the flush/dwell helpers can
// emit into from ordinary object G-code, where an unrelated M400 (e.g. GCodeProcessor's
// pre-heat injector, gated off here since neither test sets enable_pre_heating) would
// otherwise create a false match.
static std::string wipe_tower_regions(const std::string &gcode)
{
    std::string regions;
    size_t pos = 0;
    while (true) {
        size_t start = gcode.find("WIPE_TOWER_START", pos);
        if (start == std::string::npos)
            break;
        size_t end = gcode.find("WIPE_TOWER_END", start);
        if (end == std::string::npos)
            break;
        regions += gcode.substr(start, end - start);
        pos = end + 1;
    }
    return regions;
}

// A per-layer toolchange between the wall and infill filaments, same shape as
// test_multifilament.cpp's "Each feature prints with its assigned filament", so the wipe
// tower actually runs its toolchange path (and so `flush_planner_queue()`) on every layer.
static DynamicPrintConfig wipe_tower_toolchange_config(const std::string &gcode_flavor)
{
    return multifilament_config(2, {
        { "sparse_infill_filament_id",  1 },
        { "internal_solid_filament_id", 1 },
        { "top_surface_filament_id",    1 },
        { "bottom_surface_filament_id", 1 },
        { "outer_wall_filament_id",     2 },
        { "inner_wall_filament_id",     2 },
        { "enable_prime_tower",         true },
        { "gcode_flavor",               gcode_flavor },
    });
}

// Slices a 20mm cube under `config`. Not just `Test::slice(...)`: a brand-new Print's first
// `apply()` call still has no per-feature regions built, so it undercounts the filaments in
// use and lets DynamicPrintConfig::normalize_fdm_2's "single filament" rule turn
// `enable_prime_tower` back off before the wipe tower ever runs. Applying the same config a
// second time, once init_print's first apply has settled those regions, lets that count see
// both filaments so the prime tower stays on.
static std::string slice_with_prime_tower(const DynamicPrintConfig &config)
{
    Print print;
    Model model;
    init_print({ cube(20) }, print, model, config);
    print.apply(model, config);
    return gcode(print);
}

TEST_CASE("Klipper's wipe tower toolchanges flush the planner queue with M400 in exported G-code", "[WipeTower]")
{
    const std::string gcode = slice_with_prime_tower(wipe_tower_toolchange_config("klipper"));
    REQUIRE_THAT(gcode, Catch::Matchers::ContainsSubstring("WIPE_TOWER_START"));

    const std::string tower = wipe_tower_regions(gcode);
    CHECK_THAT(tower, Catch::Matchers::ContainsSubstring("M400"));
    CHECK_THAT(tower, !Catch::Matchers::ContainsSubstring("G4 S0"));
}

TEST_CASE("Marlin's wipe tower toolchanges keep the zero-dwell flush in exported G-code", "[WipeTower]")
{
    const std::string gcode = slice_with_prime_tower(wipe_tower_toolchange_config("marlin"));
    REQUIRE_THAT(gcode, Catch::Matchers::ContainsSubstring("WIPE_TOWER_START"));

    const std::string tower = wipe_tower_regions(gcode);
    CHECK_THAT(tower, Catch::Matchers::ContainsSubstring("G4 S0"));
    CHECK_THAT(tower, !Catch::Matchers::ContainsSubstring("M400"));
}
