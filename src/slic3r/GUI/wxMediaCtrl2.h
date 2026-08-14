//
//  wxMediaCtrl2.h
//  libslic3r_gui
//
//  Created by cmguo on 2021/12/7.
//

#ifndef wxMediaCtrl2_h
#define wxMediaCtrl2_h

#include "wx/uri.h"
#include "wx/mediactrl.h"

wxDECLARE_EVENT(EVT_MEDIA_CTRL_STAT, wxCommandEvent);

void wxMediaCtrl_OnSize(wxWindow * ctrl, wxSize const & videoSize, int width, int height);

#if defined(__LINUX__) && defined(__WXGTK__)
typedef struct _GstElement GstElement;
#endif

class wxMediaCtrl2 : public wxMediaCtrl
{
public:
    wxMediaCtrl2(wxWindow *parent);
    ~wxMediaCtrl2();

    void Load(wxURI url);

    void Play();

    void Stop();

    void SetIdleImage(wxString const & image);

    wxMediaState GetState();

    int GetLastError() const;

    wxSize GetVideoSize() const;

protected:
    wxSize DoGetBestSize() const override;

    void DoSetSize(int x, int y, int width, int height, int sizeFlags) override;

#ifdef __WIN32__
    WXLRESULT MSWWindowProc(WXUINT   nMsg,
                            WXWPARAM wParam,
                            WXLPARAM lParam) override;
#endif

private:
#if defined(__LINUX__) && defined(__WXGTK__)
    bool CreateGtkSinkPlayer();
    void DestroyGtkSinkPlayer();
    void PostGtkSinkStateEvent(int id = 0);

    bool m_native_wayland = false;
    bool m_use_gtk_sink = false;
    wxString m_gtk_sink_error;
    bool m_gtk_sink_error_notified = false;
    GstElement *m_gtk_playbin = nullptr;
    GstElement *m_gtk_sink = nullptr;
    unsigned int m_gtk_bus_watch_id = 0;
    wxWindow *m_gtk_video_window = nullptr;
    wxMediaState m_gtk_state = wxMEDIASTATE_STOPPED;
#endif
    wxString m_idle_image;
    int      m_error = 0;
    bool     m_loaded = false;
    wxSize   m_video_size{16, 9};
};

#endif /* wxMediaCtrl2_h */
