// recompute_mixed_slot_colors lives in libslic3r_gui; this is the only suite that links it.
// Same Windows include prologue as test_dev_mapping.cpp (wx pulls in <windows.h>; keep
// WIN32_LEAN_AND_MEAN / NOMINMAX ahead of the Catch2 headers).
#ifdef WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <Windows.h>
#endif

#include <catch2/catch_all.hpp>

#include <wx/colour.h>
#include <wx/string.h>

#include "libslic3r/FilamentMixer.hpp"
#include "libslic3r/PrintConfig.hpp"
#include "slic3r/GUI/FilamentBitmapUtils.hpp"

using namespace Slic3r;
using Slic3r::GUI::recompute_mixed_slot_colors;

namespace {

// Two physical slots (1 = red, 2 = blue) and mixed slot 3 built from them.
DynamicPrintConfig mixed_config(const std::string& components = "1,2", const std::string& ratios = "0.5,0.5")
{
    DynamicPrintConfig cfg;
    cfg.set_key_value("filament_is_mixed", new ConfigOptionBools({false, false, true}));
    cfg.set_key_value("filament_mixed_components", new ConfigOptionStrings({"", "", components}));
    cfg.set_key_value("filament_mixed_sublayer_ratios", new ConfigOptionStrings({"", "", ratios}));
    cfg.set_key_value("filament_mixed_gradient", new ConfigOptionBools({false, false, false}));
    cfg.set_key_value("filament_colour", new ConfigOptionStrings({"#FF0000", "#0000FF", "#000000"}));
    return cfg;
}

wxColour expected_blend(const std::vector<std::string>& hex, const std::vector<int>& weights)
{
    return wxColour(wxString(blend_color_multi(hex, weights)));
}

// Compare channels one at a time so a failure names the channel.
void require_same_rgb(const wxColour& actual, const wxColour& expected)
{
    REQUIRE(int(actual.Red()) == int(expected.Red()));
    REQUIRE(int(actual.Green()) == int(expected.Green()));
    REQUIRE(int(actual.Blue()) == int(expected.Blue()));
}

} // namespace

TEST_CASE("recompute_mixed_slot_colors blends a mixed slot from its components' colours", "[FilamentBitmapUtils]")
{
    std::vector<wxColour> colors{wxColour(255, 0, 0), wxColour(0, 0, 255)};
    recompute_mixed_slot_colors(colors, mixed_config());

    REQUIRE(colors.size() == 3);
    require_same_rgb(colors[2], expected_blend({"#FF0000", "#0000FF"}, {5000, 5000}));
    REQUIRE(int(colors[2].Alpha()) == 255);
    // Physical slots are left alone.
    require_same_rgb(colors[0], wxColour(255, 0, 0));
    require_same_rgb(colors[1], wxColour(0, 0, 255));
}

TEST_CASE("recompute_mixed_slot_colors leaves the colours alone without mixed slots", "[FilamentBitmapUtils]")
{
    std::vector<wxColour> colors{wxColour(255, 0, 0), wxColour(0, 0, 255)};

    SECTION("no mixed keys at all") {
        recompute_mixed_slot_colors(colors, DynamicPrintConfig{});
    }
    SECTION("mixed flags present but all false") {
        DynamicPrintConfig cfg;
        cfg.set_key_value("filament_is_mixed", new ConfigOptionBools({false, false}));
        cfg.set_key_value("filament_mixed_components", new ConfigOptionStrings({"", ""}));
        recompute_mixed_slot_colors(colors, cfg);
    }
    REQUIRE(colors.size() == 2);
    require_same_rgb(colors[0], wxColour(255, 0, 0));
    require_same_rgb(colors[1], wxColour(0, 0, 255));
}

TEST_CASE("recompute_mixed_slot_colors falls back to grey for a broken component reference", "[FilamentBitmapUtils]")
{
    const wxColour grey(128, 128, 128, 255);
    std::vector<wxColour> colors{wxColour(255, 0, 0), wxColour(0, 0, 255)};

    SECTION("dangling component id") {
        recompute_mixed_slot_colors(colors, mixed_config("1,9"));
    }
    SECTION("empty component list") {
        recompute_mixed_slot_colors(colors, mixed_config(""));
    }
    REQUIRE(colors.size() == 3);
    require_same_rgb(colors[2], grey);
}

TEST_CASE("recompute_mixed_slot_colors uses the project colour when a slot colour is unset", "[FilamentBitmapUtils]")
{
    // Slot 2 carries no colour in the vector; filament_colour[1] = "#0000FF" is used instead.
    std::vector<wxColour> colors{wxColour(255, 0, 0), wxColour()};
    recompute_mixed_slot_colors(colors, mixed_config());
    require_same_rgb(colors[2], expected_blend({"#FF0000", "#0000FF"}, {5000, 5000}));
}

TEST_CASE("recompute_mixed_slot_colors blends a gradient slot from its end points only", "[FilamentBitmapUtils]")
{
    DynamicPrintConfig cfg;
    cfg.set_key_value("filament_is_mixed", new ConfigOptionBools({false, false, false, true}));
    cfg.set_key_value("filament_mixed_components", new ConfigOptionStrings({"", "", "", "1,2,3"}));
    cfg.set_key_value("filament_mixed_sublayer_ratios", new ConfigOptionStrings({"", "", "", "0.2,0.3,0.5"}));
    cfg.set_key_value("filament_mixed_gradient", new ConfigOptionBools({false, false, false, true}));
    cfg.set_key_value("filament_colour", new ConfigOptionStrings({"#FF0000", "#00FF00", "#0000FF", "#000000"}));

    std::vector<wxColour> colors{wxColour(255, 0, 0), wxColour(0, 255, 0), wxColour(0, 0, 255)};
    recompute_mixed_slot_colors(colors, cfg);

    REQUIRE(colors.size() == 4);
    require_same_rgb(colors[3], expected_blend({"#FF0000", "#0000FF"}, {5000, 5000}));
}

TEST_CASE("recompute_mixed_slot_colors honours the configured ratios and is idempotent", "[FilamentBitmapUtils]")
{
    std::vector<wxColour> colors{wxColour(255, 0, 0), wxColour(0, 0, 255)};
    const DynamicPrintConfig cfg = mixed_config("1,2", "0.7,0.3");
    recompute_mixed_slot_colors(colors, cfg);
    const wxColour first = colors[2];
    // The configured 70/30 ratio must reach the blend (it is not the equal-share default).
    require_same_rgb(first, expected_blend({"#FF0000", "#0000FF"}, {7000, 3000}));
    REQUIRE(first != expected_blend({"#FF0000", "#0000FF"}, {5000, 5000}));
    recompute_mixed_slot_colors(colors, cfg);
    require_same_rgb(colors[2], first);
}
