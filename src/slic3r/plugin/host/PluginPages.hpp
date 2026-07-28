#pragma once

#include <slic3r/plugin/PythonPluginInterface.hpp>
#include <slic3r/plugin/pluginTypes/pages/PagesPluginCapability.hpp>

#include <atomic>
#include <map>
#include <memory>
#include <string>

#include <wx/panel.h>
#include <wx/webview.h>

class Notebook;

namespace Slic3r {

class PluginPage : public wxPanel
{
public:
    PluginPage(wxWindow* parent, std::shared_ptr<PagesPluginCapability> capability);
    ~PluginPage() override;

    PluginPage() = delete;

    bool is_valid() const { return m_browser != nullptr && m_cap != nullptr; }
    void detach_capability();
    void on_bootstrap_event(wxWebViewEvent& event);
    void on_new_window(wxWebViewEvent& event);
    void on_script_message(wxWebViewEvent& event);
    void push_message(const std::string& message);

private:
    void load_plugin_content();
    wxString bootstrap_url() const;
    wxString web_base_url() const;

    wxWebView*                                      m_browser{nullptr};
    std::shared_ptr<PagesPluginCapability>          m_cap;
    std::shared_ptr<std::atomic<PluginPage*>>       m_lifetime;
    bool                                             m_content_loaded{false};
};

class PluginPages
{
public:
    PluginPages() = default;
    ~PluginPages();

    PluginPages(const PluginPages&) = delete;
    PluginPages& operator=(const PluginPages&) = delete;

    void initialize(Notebook* parent);
    void shutdown();

    void on_cap_register(const PluginCapabilityId& id);
    void on_cap_deregister(const PluginCapabilityId& id);
    void on_plugin_register(const std::string& plugin_key);
    void on_plugin_deregister(const std::string& plugin_key);

private:
    std::shared_ptr<PagesPluginCapability> get_pages_cap(const PluginCapabilityId& id, bool is_enabled) const;
    void remove_page(const PluginCapabilityId& id);

    std::map<PluginCapabilityId, PluginPage*> m_pages;
    Notebook*                                  m_parent{nullptr};
};

} // namespace Slic3r
