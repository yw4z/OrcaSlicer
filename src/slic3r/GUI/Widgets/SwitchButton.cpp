#include "SwitchButton.hpp"
#include "Label.hpp"
#include "StaticBox.hpp"

#include "../wxExtensions.hpp"
#include "../GUI_App.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "../Utils/MacDarkMode.hpp"
#include "../Utils/WxFontUtils.hpp"
#ifdef __APPLE__
#include "libslic3r/MacUtils.hpp"
#endif

#include <wx/dcmemory.h>
#include <wx/dcclient.h>
#include <wx/dcgraph.h>

wxDEFINE_EVENT(wxCUSTOMEVT_SWITCH_POS, wxCommandEvent);

SwitchButton::SwitchButton(wxWindow* parent, wxWindowID id)
	: wxBitmapToggleButton(parent, id, wxNullBitmap, wxDefaultPosition, wxDefaultSize, wxBORDER_NONE | wxBU_EXACTFIT)
	, m_on(this, "toggle_on", 16)
	, m_off(this, "toggle_off", 16)
    , text_color(std::pair{0xfffffe, (int) StateColor::Checked}, std::pair{0x6B6B6B, (int) StateColor::Normal})
	, track_color(0xD9D9D9)
    , thumb_color(std::pair{0x009688, (int) StateColor::Checked}, std::pair{0xD9D9D9, (int) StateColor::Normal})
{
	SetBackgroundColour(StaticBox::GetParentBackgroundColor(parent));
	Bind(wxEVT_TOGGLEBUTTON, [this](auto& e) { update(); e.Skip(); });
	SetFont(Label::Body_12);
	Rescale();
}

void SwitchButton::SetLabels(wxString const& lbl_on, wxString const& lbl_off)
{
	labels[0] = lbl_on;
	labels[1] = lbl_off;
	Rescale();
}

void SwitchButton::SetTextColor(StateColor const& color)
{
    text_color = color;
    Rescale();
}

void SwitchButton::SetTextColor2(StateColor const &color)
{
    text_color2 = color;
    Rescale();
}

void SwitchButton::SetTrackColor(StateColor const& color)
{
    track_color = color;
    Rescale();
}

void SwitchButton::SetThumbColor(StateColor const& color)
{
    thumb_color = color;
    Rescale();
}

void SwitchButton::SetValue(bool value)
{
    if (value != GetValue()) {
        wxBitmapToggleButton::SetValue(value);
        update();
    }
}

bool SwitchButton::SetBackgroundColour(const wxColour& colour)
{
    if (wxBitmapToggleButton::SetBackgroundColour(colour)) {
        Rescale();
        return true;
    }

    return false;
}

void SwitchButton::Rescale()
{
	if (labels[0].IsEmpty()) {
		m_on.msw_rescale();
		m_off.msw_rescale();
	}
	else {
        wxBitmapToggleButton::SetBackgroundColour(StaticBox::GetParentBackgroundColor(GetParent()));
#ifdef __WXOSX__
        auto scale = Slic3r::GUI::mac_max_scaling_factor();
        int BS = (int) scale;
#else
        constexpr int BS = 1;
#endif
		wxSize thumbSize;
		wxSize trackSize;
		wxClientDC dc(this);
#ifdef __WXOSX__
        dc.SetFont(dc.GetFont().Scaled(scale));
#endif
        wxSize textSize[2];
		{
			textSize[0] = dc.GetTextExtent(labels[0]);
			textSize[1] = dc.GetTextExtent(labels[1]);
		}
		float fontScale = 0;
		{
			thumbSize = textSize[0];
			auto size = textSize[1];
			if (size.x > thumbSize.x) thumbSize.x = size.x;
			else size.x = thumbSize.x;
			thumbSize.x += BS * 12;
			thumbSize.y += BS * 6;
			trackSize.x = thumbSize.x + size.x + BS * 10;
			trackSize.y = thumbSize.y + BS * 2;
            auto maxWidth = GetMaxWidth();
#ifdef __WXOSX__
            maxWidth *= scale;
#endif
			if (trackSize.x > maxWidth) {
                fontScale   = float(maxWidth) / trackSize.x;
                thumbSize.x -= (trackSize.x - maxWidth) / 2;
                trackSize.x = maxWidth;
			}
		}
		for (int i = 0; i < 2; ++i) {
			wxMemoryDC memdc(&dc);
#ifdef __WXMSW__
			wxBitmap bmp(trackSize.x, trackSize.y);
			memdc.SelectObject(bmp);
			memdc.SetBackground(wxBrush(GetBackgroundColour()));
			memdc.Clear();
#else
            wxImage image(trackSize);
            image.InitAlpha();
            memset(image.GetAlpha(), 0, trackSize.GetWidth() * trackSize.GetHeight());
            wxBitmap bmp(std::move(image));
            memdc.SelectObject(bmp);
#endif
            memdc.SetFont(dc.GetFont());
#ifdef __WXMSW__
            const double scale = GetDPIScaleFactor();
			fontScale = scale;
#endif
            if (fontScale) {
                memdc.SetFont(dc.GetFont().Scaled(fontScale));
                textSize[0] = memdc.GetTextExtent(labels[0]);
                textSize[1] = memdc.GetTextExtent(labels[1]);
			}
			auto state = i == 0 ? StateColor::Enabled : (StateColor::Checked | StateColor::Enabled);
            {
#ifdef __WXMSW__
				wxGCDC dc2(memdc);
#else
                wxDC &dc2(memdc);
#endif
				dc2.SetBrush(wxBrush(track_color.colorForStates(state)));
				dc2.SetPen(wxPen(track_color.colorForStates(state)));
                dc2.DrawRoundedRectangle(wxRect({0, 0}, trackSize), trackSize.y / 2.0);
				dc2.SetBrush(wxBrush(thumb_color.colorForStates(StateColor::Checked | StateColor::Enabled)));
				dc2.SetPen(wxPen(thumb_color.colorForStates(StateColor::Checked | StateColor::Enabled)));
                dc2.DrawRoundedRectangle(wxRect({ i == 0 ? BS : (trackSize.x - thumbSize.x - BS), BS}, thumbSize), thumbSize.y / 2.0);
			}
            memdc.SetTextForeground(text_color.colorForStates(state ^ StateColor::Checked));
            auto text_y = BS + (thumbSize.y - textSize[0].y) / 2;
#ifdef __APPLE__
            if (Slic3r::is_mac_version_15()) {
                text_y -= FromDIP(2);
            }
#endif
            memdc.DrawText(labels[0], {BS + (thumbSize.x - textSize[0].x) / 2, text_y});
            memdc.SetTextForeground(text_color2.count() == 0 ? text_color.colorForStates(state) : text_color2.colorForStates(state));
            auto text_y_1 = BS + (thumbSize.y - textSize[1].y) / 2;
#ifdef __APPLE__
            if (Slic3r::is_mac_version_15()) {
                text_y_1 -= FromDIP(2);
            }
#endif
            memdc.DrawText(labels[1], {trackSize.x - thumbSize.x - BS + (thumbSize.x - textSize[1].x) / 2, text_y_1});
			memdc.SelectObject(wxNullBitmap);
#ifdef __WXOSX__
            bmp = wxBitmap(bmp.ConvertToImage(), -1, scale);
#elif defined(__WXMSW__)
            bmp.SetScaleFactor(scale); // ORCA
#endif
			(i == 0 ? m_off : m_on).bmp() = bmp;
		}
	}
	update();
#ifdef __WXGTK__
	wxSize bestSize = GetBestSize();
	bestSize.IncTo(m_on.GetBmpSize());
	SetSize(bestSize);
	SetMinSize(bestSize);
#else
	SetSize(m_on.GetBmpSize());
#endif
}

void SwitchButton::update()
{
	SetBitmap((GetValue() ? m_on : m_off).bmp());
}

ModeSwitchButton::ModeSwitchButton(wxWindow* parent, wxWindowID id)
{
    background_color = StateColor(
        std::make_pair(wxColour("#D0D0D4"), (int) StateColor::Disabled),
        std::make_pair(wxColour("#D9D9D9"), (int) StateColor::Normal)
    );
    border_color = StateColor(
        std::make_pair(wxColour("#D0D0D4"), (int) StateColor::Disabled),
        std::make_pair(wxColour("#D0D0D4"), (int) StateColor::Hovered | ~StateColor::Focused),
        std::make_pair(wxColour("#009688"), (int) StateColor::Focused),
        std::make_pair(wxColour("#D9D9D9"), (int) StateColor::Normal)
    );
    dot_active = StateColor(
        std::make_pair(wxColour("#ACACAC"), (int) StateColor::Disabled),
        std::make_pair(wxColour("#26A69A"), (int) StateColor::Hovered),
        std::make_pair(wxColour("#009688"), (int) StateColor::Normal)
    );
    dot_dimmed = StateColor(
        std::make_pair(wxColour("#ACACAC"), (int) StateColor::Disabled),
        std::make_pair(wxColour("#ACACAC"), (int) StateColor::Normal)
    );

    StaticBox::Create(parent, id, wxDefaultPosition, wxDefaultSize, 0);
    SetBackgroundColour(StaticBox::GetParentBackgroundColor(parent));
    SetCursor(wxCursor(wxCURSOR_HAND));

    m_tooltips[0] = _L("Simple settings");
    m_tooltips[1] = _L("Advanced settings");
    m_tooltips[2] = _L("Expert settings");

    Bind(wxEVT_LEFT_DOWN, &ModeSwitchButton::mouseDown, this);
    Bind(wxEVT_LEFT_UP, &ModeSwitchButton::mouseReleased, this);
    Bind(wxEVT_LEFT_DCLICK, &ModeSwitchButton::mouseDown, this);
    Bind(wxEVT_MOUSE_CAPTURE_LOST, &ModeSwitchButton::mouseCaptureLost, this);

    Rescale();
}

void ModeSwitchButton::SetSelection(int selection)
{
    m_selection = std::clamp(selection, 0, 2);
    update_tooltip();
    Refresh();
}

void ModeSwitchButton::SelectAndNotify(int selection)
{
    if (!IsEnabled())
        return;

    SetSelection(selection);
    Slic3r::GUI::wxGetApp().save_mode(m_selection);
}

void ModeSwitchButton::Rescale()
{
    wxClientDC dc(this);
#ifdef __WXOSX__
    auto scale = Slic3r::GUI::mac_max_scaling_factor();
    int BS = (int) scale;
    dc.SetFont(dc.GetFont().Scaled(scale));
#else
    constexpr int BS = 1;
#endif

    wxSize textSize = dc.GetTextExtent("yY");
    int height = textSize.y + BS * 4;

    const wxSize button_size = wxSize(height * 2.8, height);
    SetMinSize(button_size);
    SetMaxSize(button_size);
    SetSize(button_size);
    SetCornerRadius(button_size.y / 2.0);
    Refresh();
}

bool ModeSwitchButton::Enable(bool enable /* = true */)
{
    const bool changed = StaticBox::Enable(enable);
    if (changed)
        Refresh();
    return changed;
}

void ModeSwitchButton::doRender(wxDC& dc)
{
    dc.SetPen(*wxTRANSPARENT_PEN);
    dc.SetBrush(wxBrush(GetBackgroundColour()));
    dc.DrawRectangle(GetClientRect());

    const wxRect bounds = GetClientRect();
    if (bounds.width <= 0 || bounds.height <= 0)
        return;

    int    states      = state_handler.states();
    double half_height = bounds.height / 2.0;
    double dot_dist    = (bounds.width - bounds.height) * 0.50;

    dc.SetPen(wxPen(border_color.colorForStates(states), 1));
    dc.SetBrush(wxBrush(background_color.colorForStates(states)));
    dc.DrawRoundedRectangle(bounds, half_height);

    dc.SetPen(*wxTRANSPARENT_PEN);
    for (int idx = 0; idx < 3; ++idx) {
        dc.SetBrush(wxBrush(idx <= m_selection ? dot_active.colorForStates(states) : dot_dimmed.colorForStates(states)));
        dc.DrawCircle(wxPoint(half_height + dot_dist * idx, half_height), bounds.height * (double)(idx == m_selection ? 0.35 : 0.20));
    }
}

void ModeSwitchButton::mouseDown(wxMouseEvent& event)
{
    if (!IsEnabled()) {
        event.Skip();
        return;
    }

    m_pressed = true;
    if (!HasCapture())
        CaptureMouse();

    Refresh();

    event.Skip();
}

void ModeSwitchButton::mouseReleased(wxMouseEvent& event)
{
    if (m_pressed) {
        m_pressed = false;
        if (HasCapture())
            ReleaseMouse();

        if (GetClientRect().Contains(event.GetPosition()))
            SelectAndNotify(hit_test_selection(event.GetPosition()));

        Refresh();
    }

    event.Skip();
}

void ModeSwitchButton::mouseCaptureLost(wxMouseCaptureLostEvent& event)
{
    m_pressed = false;
    Refresh();
    event.Skip();
}

int ModeSwitchButton::hit_test_selection(const wxPoint& point) const
{
    const int width = std::max(1, GetClientSize().x);
    const int x = std::clamp(point.x, 0, width - 1);
    return std::clamp((x * 3) / width, 0, 2);
}

void ModeSwitchButton::update_tooltip()
{
    SetToolTip(m_tooltips[m_selection]);
}

SwitchBoard::SwitchBoard(wxWindow *parent, wxString leftL, wxString right, wxSize size)
 : wxWindow(parent, wxID_ANY, wxDefaultPosition, size)
{
#ifdef __WINDOWS__
    SetDoubleBuffered(true);
#endif //__WINDOWS__

    SetBackgroundColour(*wxWHITE);
	leftLabel = leftL;
    rightLabel = right;

	SetMinSize(size);
	SetMaxSize(size);

    Bind(wxEVT_PAINT, &SwitchBoard::paintEvent, this);
    Bind(wxEVT_LEFT_DOWN, &SwitchBoard::on_left_down, this);

    Bind(wxEVT_ENTER_WINDOW, [this](auto &e) { SetCursor(wxCURSOR_HAND); });
    Bind(wxEVT_LEAVE_WINDOW, [this](auto &e) { SetCursor(wxCURSOR_ARROW); });
}

void SwitchBoard::updateState(wxString target)
{
    if (target.empty()) {
        if (!switch_left && !switch_right) {
            return;
        }

        switch_left = false;
        switch_right = false;
    } else {
        if (target == "left") {
            if (switch_left && !switch_right) {
                return;
            }

            switch_left = true;
            switch_right = false;
        } else if (target == "right") {
            if (!switch_left && switch_right) {
                return;
            }

            switch_left  = false;
            switch_right = true;
        }
    }

    Refresh();
}

void SwitchBoard::paintEvent(wxPaintEvent &evt)
{
    wxPaintDC dc(this);
    render(dc);
}

void SwitchBoard::render(wxDC &dc)
{
#ifdef __WXMSW__
    wxSize     size = GetSize();
    wxMemoryDC memdc;
    wxBitmap   bmp(size.x, size.y);
    memdc.SelectObject(bmp);
    memdc.Blit({0, 0}, size, &dc, {0, 0});

    {
        wxGCDC dc2(memdc);
        doRender(dc2);
    }

    memdc.SelectObject(wxNullBitmap);
    dc.DrawBitmap(bmp, 0, 0);
#else
    doRender(dc);
#endif
}

void SwitchBoard::doRender(wxDC &dc)
{
    wxColour disable_color = wxColour(0xCECECE);

    dc.SetPen(*wxTRANSPARENT_PEN);

    if (is_enable) {dc.SetBrush(wxBrush(0xeeeeee));
    } else {dc.SetBrush(disable_color);}
    dc.DrawRoundedRectangle(0, 0, GetSize().x, GetSize().y, 8);

	/*left*/
    if (switch_left) {
        is_enable ? dc.SetBrush(wxBrush(wxColour(0, 150, 136))) : dc.SetBrush(disable_color);
        dc.DrawRoundedRectangle(0, 0, GetSize().x / 2, GetSize().y, 8);
	}

    if (switch_left) {
		dc.SetTextForeground(*wxWHITE);
    } else {
        dc.SetTextForeground(0x333333);
	}

    dc.SetFont(::Label::Body_13);
    Slic3r::GUI::WxFontUtils::get_suitable_font_size(0.6 * GetSize().GetHeight(), dc);

    auto left_txt_size = dc.GetTextExtent(leftLabel);
    dc.DrawText(leftLabel, wxPoint((GetSize().x / 2 - left_txt_size.x) / 2, (GetSize().y - left_txt_size.y) / 2));

	/*right*/
    if (switch_right) {
        if (is_enable) {dc.SetBrush(wxBrush(wxColour(0, 150, 136)));
        } else {dc.SetBrush(disable_color);}
        dc.DrawRoundedRectangle(GetSize().x / 2, 0, GetSize().x / 2, GetSize().y, 8);
	}

    auto right_txt_size = dc.GetTextExtent(rightLabel);
    if (switch_right) {
        dc.SetTextForeground(*wxWHITE);
    } else {
        dc.SetTextForeground(0x333333);
    }
    dc.DrawText(rightLabel, wxPoint((GetSize().x / 2 - right_txt_size.x) / 2 + GetSize().x / 2, (GetSize().y - right_txt_size.y) / 2));

}

void SwitchBoard::on_left_down(wxMouseEvent &evt)
{
    if (!is_enable) {
        return;
    }
    int index = -1;
    auto pos = ClientToScreen(evt.GetPosition());
    auto rect = ClientToScreen(wxPoint(0, 0));

    if (pos.x > 0 && pos.x < rect.x + GetSize().x / 2) {
        switch_left = true;
        switch_right = false;
        index = 1;
    } else {
        switch_left  = false;
        switch_right = true;
        index = 0;
    }

    if (auto_disable_when_switch)
    {
        is_enable = false;// make it disable while switching
    }
    Refresh();

    wxCommandEvent event(wxCUSTOMEVT_SWITCH_POS);
    event.SetInt(index);
    wxPostEvent(this, event);
}

bool SwitchBoard::Enable(bool enable /* = true */)
{
    if (is_enable == enable)
    {
        return false;
    }

    is_enable = enable;
    Refresh();
    return true;
}