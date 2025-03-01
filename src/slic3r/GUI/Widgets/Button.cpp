#include "Button.hpp"
#include "Label.hpp"

#include <wx/dcgraph.h>

BEGIN_EVENT_TABLE(Button, StaticBox)

EVT_LEFT_DOWN(Button::mouseDown)
EVT_LEFT_UP(Button::mouseReleased)
EVT_MOUSE_CAPTURE_LOST(Button::mouseCaptureLost)
EVT_KEY_DOWN(Button::keyDownUp)
EVT_KEY_UP(Button::keyDownUp)

// catch paint events
EVT_PAINT(Button::paintEvent)

END_EVENT_TABLE()

/*
 * Called by the system of by wxWidgets when the panel needs
 * to be redrawn. You can also trigger this call by
 * calling Refresh()/Update().
 */

Button::Button()
    : paddingSize(10, 8)
{
    background_color = StateColor(
        std::make_pair(0xF0F0F1, (int) StateColor::Disabled),
        std::make_pair(0x52c7b8, (int) StateColor::Hovered | StateColor::Checked),
        std::make_pair(0x009688, (int) StateColor::Checked),
        std::make_pair(*wxLIGHT_GREY, (int) StateColor::Hovered), 
        std::make_pair(*wxWHITE, (int) StateColor::Normal));
    text_color       = StateColor(
        std::make_pair(*wxLIGHT_GREY, (int) StateColor::Disabled), 
        std::make_pair(*wxBLACK, (int) StateColor::Normal));
}

Button::Button(wxWindow* parent, wxString text, wxString icon, long style, int iconSize, wxWindowID btn_id)
    : Button()
{
    Create(parent, text, icon, style, iconSize, btn_id);
}

bool Button::Create(wxWindow* parent, wxString text, wxString icon, long style, int iconSize, wxWindowID btn_id)
{
    StaticBox::Create(parent, btn_id, wxDefaultPosition, wxDefaultSize, style);
    state_handler.attach({&text_color});
    state_handler.update_binds();
    //BBS set default font
    SetFont(Label::Body_14);
    wxWindow::SetLabel(text);
    if (!icon.IsEmpty()) {
        //BBS set button icon default size to 20
        this->active_icon = ScalableBitmap(this, icon.ToStdString(), iconSize > 0 ? iconSize : 20);
    }
    messureSize();
    return true;
}

void Button::SetLabel(const wxString& label)
{
    wxWindow::SetLabel(label);
    messureSize();
    Refresh();
}

bool Button::SetFont(const wxFont& font)
{
    wxWindow::SetFont(font);
    messureSize();
    Refresh();
    return true;
}

void Button::SetIcon(const wxString& icon)
{
    if (!icon.IsEmpty()) {
        //BBS set button icon default size to 20
        this->active_icon = ScalableBitmap(this, icon.ToStdString(), this->active_icon.px_cnt());
    }
    else
    {
        this->active_icon = ScalableBitmap();
    }
    Refresh();
}

void Button::SetInactiveIcon(const wxString &icon)
{
    if (!icon.IsEmpty()) {
        // BBS set button icon default size to 20
        this->inactive_icon = ScalableBitmap(this, icon.ToStdString(), this->active_icon.px_cnt());
    } else {
        this->inactive_icon = ScalableBitmap();
    }
    Refresh();
}

void Button::SetMinSize(const wxSize& size)
{
    minSize = size;
    messureSize();
}

void Button::SetPaddingSize(const wxSize& size)
{
    paddingSize = size;
    messureSize();
}

void Button::SetTextColor(StateColor const& color)
{
    text_color = color;
    state_handler.update_binds();
    Refresh();
}

void Button::SetTextColorNormal(wxColor const &color)
{
    text_color.setColorForStates(color, 0);
    Refresh();
}

bool Button::Enable(bool enable)
{
    bool result = wxWindow::Enable(enable);
    if (result) {
        wxCommandEvent e(EVT_ENABLE_CHANGED);
        e.SetEventObject(this);
        GetEventHandler()->ProcessEvent(e);
    }
    return result;
}

void Button::SetCanFocus(bool canFocus) { this->canFocus = canFocus; }

void Button::SetValue(bool state)
{
    if (GetValue() == state) return;
    state_handler.set_state(state ? StateHandler::Checked : 0, StateHandler::Checked);
}

bool Button::GetValue() const { return state_handler.states() & StateHandler::Checked; }

void Button::SetCenter(bool isCenter)
{
    this->isCenter = isCenter;
}

// Button Colors           bg-Disabled bg-Pressed bg-Hover   bg-Normal  bg-Enabled fg-Disabled fg-Normal fg-Hover  br-hover
wxString btn_regular[9]  = {"#DFDFDF", "#DFDFDF", "#D4D4D4", "#DFDFDF", "#DFDFDF", "#6B6A6A", "#262E30", "#262E30", "#009688"};
wxString btn_confirm[9]  = {"#DFDFDF", "#009688", "#26A69A", "#009688", "#009688", "#6B6A6A", "#FEFEFE", "#262E30", "#3EE0D8"};
wxString btn_alert[9]    = {"#DFDFDF", "#DFDFDF", "#CD1F00", "#DFDFDF", "#DFDFDF", "#6B6A6A", "#CD1F00", "#FFFFFD", "#F43200"};
wxString btn_disabled[9] = {"#DFDFDF", "#DFDFDF", "#DFDFDF", "#DFDFDF", "#DFDFDF", "#6B6A6A", "#6B6A6A", "#262E30", "#DFDFDF"};

void Button::SetStyle(const wxString style /* Regular/Confirm/Alert/Disabled */, const wxString& type /* Choice/Window/Parameter/Compact */)
{
    // STYLES
    //   Regular / Confirm / Alert / Disabled
    // TYPES
    //   Omited      FontSize:14   SemiRounded    Expanded / full size button. ex btn->SetStyle("Regular");
    //   Compact     FontSize:10   FullyRounded   Use for less spaced areas
    //   Window      FontSize:12   FullyRounded   Use for regular windows in windows
    //   Choice      FontSize:14   SemiRounded    Use for dialog/window choice buttons
    //   Parameter   FontSize:14   SemiRounded    Use for buttons that near parameter boxes
    this->SetFont( type == "Compact" ? Label::Body_10 : 
                   type == "Window"  ? Label::Body_12 : 
                                       Label::Body_14
    );
    auto clr_arr = style == "Regular"  ? btn_regular :
                   style == "Confirm"  ? btn_confirm :
                   style == "Alert"    ? btn_alert :
                   style == "Disabled" ? btn_disabled :
                                         btn_regular;
    StateColor clr_bg = StateColor(std::pair(wxColour(clr_arr[0]), (int)StateColor::Disabled),
                                   std::pair(wxColour(clr_arr[1]), (int)StateColor::Pressed),
                                   std::pair(wxColour(clr_arr[2]), (int)StateColor::Hovered),
                                   std::pair(wxColour(clr_arr[3]), (int)StateColor::Normal),
                                   std::pair(wxColour(clr_arr[4]), (int)StateColor::Enabled)
    );
    this->SetBackgroundColor(clr_bg);
    StateColor clr_br = StateColor(std::pair(wxColour(clr_arr[0]), (int)StateColor::Disabled),
                                   std::pair(wxColour(clr_arr[1]), (int)StateColor::Pressed),
                                   std::pair(wxColour(clr_arr[8]), (int)StateColor::Hovered), // brighter color on border to highlight focus
                                   std::pair(wxColour(clr_arr[3]), (int)StateColor::Normal)
    );
    this->SetBorderColor(clr_br);
    this->SetTextColor( StateColor(std::pair(wxColour(clr_arr[5]), (int)StateColor::Disabled),
                                   std::pair(wxColour(clr_arr[7]), (int)StateColor::Hovered),
                                   std::pair(wxColour(clr_arr[6]), (int)StateColor::Normal)
    ));
    this->SetType(type);
}

void Button::SetType(const wxString type /* Choice/Window/Parameter/Compact */)
{
    // Function also rescales button
    // Omited      FontSize:14   SemiRounded    Expanded / full size button. ex btn->SetStyle("Regular");
    // Compact     FontSize:10   FullyRounded   Use for less spaced areas
    // Window      FontSize:12   FullyRounded   Use for regular windows in windows
    // Choice      FontSize:14   SemiRounded    Use for dialog/window choice buttons
    // Parameter   FontSize:14   SemiRounded    Use for buttons that near parameter boxes
    if        (type == "Compact") {
        this->SetPaddingSize(FromDIP(wxSize(8,3)));
        this->SetCornerRadius(this->FromDIP(8));
    } else if (type == "Window") {
        this->SetSize(FromDIP(wxSize(58,24)));
        this->SetMinSize(FromDIP(wxSize(58,24)));
        this->SetCornerRadius(this->FromDIP(12));
    } else if (type == "Choice") {
        this->SetMinSize(FromDIP(wxSize(100,32)));
        this->SetPaddingSize(FromDIP(wxSize(12,8)));
        this->SetCornerRadius(this->FromDIP(4));
    } else if (type == "Parameter") {
        this->SetMinSize(FromDIP(wxSize(120,26)));
        this->SetSize(FromDIP(wxSize(120,26)));
        this->SetCornerRadius(this->FromDIP(4));
    } else {
        this->SetCornerRadius(this->FromDIP(4));
    }
    this->SetBorderWidth(this->FromDIP(1));
}

void Button::Rescale()
{
    if (this->active_icon.bmp().IsOk())
        this->active_icon.msw_rescale();

    if (this->inactive_icon.bmp().IsOk())
        this->inactive_icon.msw_rescale();

    messureSize();
}

void Button::paintEvent(wxPaintEvent& evt)
{
    // depending on your system you may need to look at double-buffered dcs
    wxPaintDC dc(this);
    render(dc);
}

/*
 * Here we do the actual rendering. I put it in a separate
 * method so that it can work no matter what type of DC
 * (e.g. wxPaintDC or wxClientDC) is used.
 */
void Button::render(wxDC& dc)
{
    StaticBox::render(dc);
    int states = state_handler.states();
    wxSize size = GetSize();
    dc.SetBrush(*wxTRANSPARENT_BRUSH);
    // calc content size
    wxSize szIcon;
    wxSize szContent = textSize.GetSize();

    ScalableBitmap icon;
    if (m_selected || ((states & (int)StateColor::State::Hovered) != 0))
        icon = active_icon;
    else
        icon = inactive_icon;
    int padding = 5;
    if (icon.bmp().IsOk()) {
        if (szContent.y > 0) {
            //BBS norrow size between text and icon
            szContent.x += padding;
        }
        szIcon = icon.GetBmpSize();
        szContent.x += szIcon.x;
        if (szIcon.y > szContent.y)
            szContent.y = szIcon.y;
        if (szContent.x > size.x) {
            int d = std::min(padding, szContent.x - size.x);
            padding -= d;
            szContent.x -= d;
        }
    }
    // move to center
    wxRect rcContent = { {0, 0}, size };
    if (isCenter) {
        wxSize offset = (size - szContent) / 2;
        if (offset.x < 0) offset.x = 0;
        rcContent.Deflate(offset.x, offset.y);
    }
    // start draw
    wxPoint pt = rcContent.GetLeftTop();
    if (icon.bmp().IsOk()) {
        pt.y += (rcContent.height - szIcon.y) / 2;
        dc.DrawBitmap(icon.bmp(), pt);
        //BBS norrow size between text and icon
        pt.x += szIcon.x + padding;
        pt.y = rcContent.y;
    }
    auto text = GetLabel();
    if (!text.IsEmpty()) {
        if (pt.x + textSize.width > size.x)
            text = wxControl::Ellipsize(text, dc, wxELLIPSIZE_END, size.x - pt.x);
        pt.y += (rcContent.height - textSize.height) / 2;
        dc.SetTextForeground(text_color.colorForStates(states));
#if 0
        dc.SetBrush(*wxLIGHT_GREY);
        dc.SetPen(wxPen(*wxLIGHT_GREY));
        dc.DrawRectangle(pt, textSize.GetSize());
#endif
#ifdef __WXOSX__
        pt.y -= textSize.x / 2;
#endif
        dc.DrawText(text, pt);
    }
}

void Button::messureSize()
{
    wxClientDC dc(this);
    dc.GetTextExtent(GetLabel(), &textSize.width, &textSize.height, &textSize.x, &textSize.y);
    wxSize szContent = textSize.GetSize();
    if (this->active_icon.bmp().IsOk()) {
        if (szContent.y > 0) {
            //BBS norrow size between text and icon
            szContent.x += 5;
        }
        wxSize szIcon = this->active_icon.GetBmpSize();
        szContent.x += szIcon.x;
        if (szIcon.y > szContent.y)
            szContent.y = szIcon.y;
    }
    wxSize size = szContent + paddingSize * 2;
    if (minSize.GetHeight() > 0)
        size.SetHeight(minSize.GetHeight());

    if (minSize.GetWidth() > size.GetWidth())
        wxWindow::SetMinSize(minSize);
    else
        wxWindow::SetMinSize(size);
}

void Button::mouseDown(wxMouseEvent& event)
{
    event.Skip();
    pressedDown = true;
    if (canFocus)
        SetFocus();
    if (!HasCapture())
        CaptureMouse();
}

void Button::mouseReleased(wxMouseEvent& event)
{
    event.Skip();
    if (pressedDown) {
        pressedDown = false;
        if (HasCapture())
            ReleaseMouse();
        if (wxRect({0, 0}, GetSize()).Contains(event.GetPosition()))
            sendButtonEvent();
    }
}

void Button::mouseCaptureLost(wxMouseCaptureLostEvent &event)
{
    wxMouseEvent evt;
    mouseReleased(evt);
}

void Button::keyDownUp(wxKeyEvent &event)
{
    if (event.GetKeyCode() == WXK_SPACE || event.GetKeyCode() == WXK_RETURN) {
        wxMouseEvent evt(event.GetEventType() == wxEVT_KEY_UP ? wxEVT_LEFT_UP : wxEVT_LEFT_DOWN);
        event.SetEventObject(this);
        GetEventHandler()->ProcessEvent(evt);
        return;
    }
    if (event.GetEventType() == wxEVT_KEY_DOWN &&
        (event.GetKeyCode() == WXK_TAB || event.GetKeyCode() == WXK_LEFT || event.GetKeyCode() == WXK_RIGHT 
        || event.GetKeyCode() == WXK_UP || event.GetKeyCode() == WXK_DOWN))
        HandleAsNavigationKey(event);
    else
        event.Skip();
}

void Button::sendButtonEvent()
{
    wxCommandEvent event(wxEVT_COMMAND_BUTTON_CLICKED, GetId());
    event.SetEventObject(this);
    GetEventHandler()->ProcessEvent(event);
}

#ifdef __WIN32__

WXLRESULT Button::MSWWindowProc(WXUINT nMsg, WXWPARAM wParam, WXLPARAM lParam)
{
    if (nMsg == WM_GETDLGCODE) { return DLGC_WANTMESSAGE; }
    if (nMsg == WM_KEYDOWN) {
        wxKeyEvent event(CreateKeyEvent(wxEVT_KEY_DOWN, wParam, lParam));
        switch (wParam) {
        case WXK_RETURN: { // WXK_RETURN key is handled by default button
            GetEventHandler()->ProcessEvent(event);
            return 0;
        }
        }
    }
    return wxWindow::MSWWindowProc(nMsg, wParam, lParam);
}

#endif

bool Button::AcceptsFocus() const { return canFocus; }

int ButtonProps::ChoiceGap() { return 10; }