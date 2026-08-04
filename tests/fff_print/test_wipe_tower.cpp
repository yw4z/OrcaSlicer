#include <catch2/catch_all.hpp>

#include <string>

#include "libslic3r/GCode/WipeTower.hpp"
#include "libslic3r/PrintConfig.hpp"

using namespace Slic3r;

// The wipe tower flushes the firmware's motion queue before a command or custom-G-code
// boundary that must not be reached early (an extruder-position reset, entering custom
// G-code). Klipper acts on those the moment it parses them, and its G4 reads only P, so the
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
