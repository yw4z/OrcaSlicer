#ifndef slic3r_GUI_FilamentBitmapUtils_hpp_
#define slic3r_GUI_FilamentBitmapUtils_hpp_

#include <wx/bitmap.h>
#include <wx/colour.h>
#include <wx/dc.h>
#include <wx/gdicmn.h>
#include <vector>

// Orca: forward-declare so the header is self-contained outside libslic3r_gui's
// force-included pch (the GUI test suite includes it directly).
namespace Slic3r { class DynamicPrintConfig; }

namespace Slic3r { namespace GUI {

// Fills a rect with a west->east linear gradient by drawing solid 1px columns.
// Use instead of wxDC::GradientFillLinear, whose CoreGraphics (CGShading) backend
// fails to render on some macOS builds; solid fills are unaffected.
void fill_gradient_rect_east(wxDC& dc, const wxRect& rect, const wxColour& from, const wxColour& to);

enum class FilamentRenderMode {
    Single,
    Dual,
    Triple,
    Quadruple,
    Gradient
};

// Create a colour swatch bitmap. The render mode is chosen automatically from the
// number of colours unless force_gradient is true.
wxBitmap create_filament_bitmap(const std::vector<wxColour>& colors,
                              const wxSize& size,
                              bool force_gradient = false);

// Recompute blended representative colors for mixed (virtual) filament slots.
// Reads mixed-filament config keys from cfg and writes back into colors[i]
// for every slot where filament_is_mixed[i] is true.
void recompute_mixed_slot_colors(std::vector<wxColour>& colors,
                                 const Slic3r::DynamicPrintConfig& cfg);

}} // namespace Slic3r::GUI

#endif // slic3r_GUI_FilamentBitmapUtils_hpp_