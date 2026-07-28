#include "PluginAuditManager.hpp"

#include "../Utils/OrcaCloudServiceAgent.hpp"
#include "libslic3r/Utils.hpp"
#include "libslic3r/libslic3r.h" // GCODEVIEWER_APP_KEY, and SLIC3R_APP_KEY via libslic3r_version.h

#include <boost/algorithm/string/predicate.hpp>
#include <boost/log/trivial.hpp>

#include <algorithm>
#include <cstdlib>
#include <future>
#include <slic3r/GUI/BindDialog.hpp>
#include <slic3r/GUI/GUI_App.hpp>
#include <slic3r/plugin/PluginFsUtils.hpp>
#include <slic3r/plugin/PluginManager.hpp>
#include <utility>
#include <wx/event.h>
#include <wx/msgdlg.h>

namespace Slic3r {

// ---------------------------------------------------------------------------
// Path safety
// ---------------------------------------------------------------------------

bool is_inside_allowed_root(const boost::filesystem::path& candidate, const boost::filesystem::path& allowed_root)
{
    namespace fs = boost::filesystem;

    boost::system::error_code ec;

    // Canonicalize both paths.  weakly_canonical resolves symlinks but does
    // NOT require the path to exist — it canonicalizes the prefix that exists
    // and appends the non-existing tail lexically.
    fs::path canon_candidate = fs::weakly_canonical(candidate, ec);
    if (ec) {
        // Fall back to lexically_normal + absolute
        canon_candidate = fs::absolute(candidate, ec).lexically_normal();
        if (ec)
            canon_candidate = candidate;
    }

    fs::path canon_root = fs::weakly_canonical(allowed_root, ec);
    if (ec) {
        canon_root = fs::absolute(allowed_root, ec).lexically_normal();
        if (ec)
            canon_root = allowed_root;
    }

    // Component-wise comparison: the root must be a prefix of candidate,
    // and the next component must not be ".." or missing.
    auto cand_it  = canon_candidate.begin();
    auto cand_end = canon_candidate.end();
    auto root_it  = canon_root.begin();
    auto root_end = canon_root.end();

    const auto same_component = [](const fs::path& lhs, const fs::path& rhs) {
#ifdef _WIN32
        return boost::algorithm::iequals(lhs.native(), rhs.native());
#else
        return lhs == rhs;
#endif
    };

    // Consume matching components
    while (root_it != root_end && cand_it != cand_end && same_component(*root_it, *cand_it)) {
        ++root_it;
        ++cand_it;
    }

    // If we didn't consume the entire root, candidate is not inside it.
    if (root_it != root_end)
        return false;

    // The remaining path components must not traverse upward.
    for (auto it = cand_it; it != cand_end; ++it) {
        if (*it == "..")
            return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// ScopedPluginAuditContext
// ---------------------------------------------------------------------------

thread_local std::string PluginAuditManager::m_current_plugin_key = "";
thread_local std::string PluginAuditManager::m_current_capability_name = "";
thread_local std::vector<boost::filesystem::path> PluginAuditManager::m_scoped_allowed_roots;
thread_local bool PluginAuditManager::m_audit_denial_pending = false;
thread_local bool PluginAuditManager::m_has_last_violation = false;
thread_local AuditViolation PluginAuditManager::m_last_violation;

ScopedPluginAuditContext::ScopedPluginAuditContext(const std::string& plugin_key,
                                                   const std::string& capability_name)
    : m_previous_id(PluginAuditManager::instance().current_plugin())
    , m_previous_capability(PluginAuditManager::instance().current_capability())
    , m_previous_scoped_roots(PluginAuditManager::m_scoped_allowed_roots)
{
    PluginAuditManager::instance().set_current_plugin(plugin_key);
    PluginAuditManager::instance().set_current_capability(capability_name);
    PluginAuditManager::m_scoped_allowed_roots.clear();
}

ScopedPluginAuditContext::~ScopedPluginAuditContext()
{
    PluginAuditManager::instance().set_current_plugin(m_previous_id);
    PluginAuditManager::instance().set_current_capability(m_previous_capability);
    PluginAuditManager::m_scoped_allowed_roots = std::move(m_previous_scoped_roots);
}

// ---------------------------------------------------------------------------
// PluginAuditManager
// ---------------------------------------------------------------------------

PluginAuditManager& PluginAuditManager::instance()
{
    static PluginAuditManager mgr;
    return mgr;
}

void PluginAuditManager::set_current_plugin(const std::string& plugin_key) { m_current_plugin_key = plugin_key; }

std::string PluginAuditManager::current_plugin() const { return m_current_plugin_key; }

void PluginAuditManager::clear_current_plugin() { m_current_plugin_key.clear(); }

void PluginAuditManager::set_current_capability(const std::string& capability_name) { m_current_capability_name = capability_name; }

std::string PluginAuditManager::current_capability() const { return m_current_capability_name; }

void PluginAuditManager::clear_current_capability() { m_current_capability_name.clear(); }

void PluginAuditManager::add_global_allowed_root(const boost::filesystem::path& root)
{
    if (root.empty())
        return;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_global_allowed_roots.push_back(root);
    BOOST_LOG_TRIVIAL(info) << "[AUDIT] Global allowed root: " << root.string();
}

void PluginAuditManager::add_scoped_allowed_root(const boost::filesystem::path& root)
{
    if (root.empty())
        return;

    m_scoped_allowed_roots.push_back(root);
    BOOST_LOG_TRIVIAL(info) << "[AUDIT] Scoped allowed root for plugin " << current_plugin() << ": " << root.string();
}

// ---------------------------------------------------------------------------
// Denied filenames
// ---------------------------------------------------------------------------

void PluginAuditManager::add_denied_filename(const std::string& filename)
{
    if (filename.empty())
        return;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_denied_filenames.push_back(filename);
    BOOST_LOG_TRIVIAL(info) << "[AUDIT] Denied filename: " << filename;
}

std::vector<std::string> PluginAuditManager::default_denied_filenames()
{
    // AppConfig::config_path() picks .conf vs .ini on the USE_JSON_CONFIG ifdef and the app key
    // on its mode, and is a non-static member we cannot call without an instance.  Denying all
    // four names is cheaper and more robust than replicating that; the two unused names cost one
    // string comparison each.  Single-sourced here so install_hook() and the tests seed from the
    // same list and cannot drift apart.
    return {
        SLIC3R_APP_KEY ".conf",
        GCODEVIEWER_APP_KEY ".conf",
        SLIC3R_APP_KEY ".ini",
        GCODEVIEWER_APP_KEY ".ini",
        secret_constants::USER_SECRET_FILENAME,
    };
}

bool PluginAuditManager::is_denied_filename(const boost::filesystem::path& candidate) const
{
    // Match on the base name alone, with no path resolution.  Traversal is handled for free,
    // because filename() of data_dir()/plugins/../OrcaSlicer.conf is already "OrcaSlicer.conf",
    // and the prefix rule covers the .bak/.tmp companions that hold the same secrets plus Windows
    // alternate data streams ("OrcaSlicer.conf:stream").  A plugin that launders a denied file
    // through a symlink, a hardlink, a subprocess, or a Windows 8.3 short name is out of scope
    // (see the design doc): this blocks direct access, not an actively evasive plugin.
    const std::string filename = candidate.filename().string();
    if (filename.empty())
        return false;

    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& denied : m_denied_filenames) {
        if (boost::algorithm::istarts_with(filename, denied))
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Policy checks
// ---------------------------------------------------------------------------

AuditDecision PluginAuditManager::check_path_access(const boost::filesystem::path& path, bool is_write)
{
    if (path.empty())
        return {true, ""};

    std::string plugin_key = current_plugin();
    if (plugin_key.empty())
        return {true, ""}; // not running inside a plugin context

    // Denied filenames are checked before the allowed roots.  The app config and the cloud
    // refresh token live directly inside data_dir(), which is a global allowed root, so a deny
    // placed any lower would be unreachable.
    if (is_denied_filename(path)) {
        BOOST_LOG_TRIVIAL(warning) << "[AUDIT] block path=" << path.string() << " is_write=" << is_write
                                   << " plugin=" << plugin_key << " reason=denied filename";
        return {false, "denied filename"};
    }

    namespace fs = boost::filesystem;
    fs::path candidate = path;

    // Resolve relative paths against the current working directory
    if (candidate.is_relative()) {
        boost::system::error_code ec;
        fs::path absolute_candidate = fs::absolute(candidate, ec);
        if (!ec)
            candidate = absolute_candidate;
    }

    for (const auto& root : m_scoped_allowed_roots) {
        if (is_inside_allowed_root(candidate, root)) {
            return {true, ""};
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& root : m_global_allowed_roots) {
            if (is_inside_allowed_root(candidate, root)) {
                return {true, ""};
            }
        }
    }

    BOOST_LOG_TRIVIAL(warning) << "[AUDIT] block path=" << candidate.string() << " is_write=" << is_write
                               << " plugin=" << plugin_key;
    return {false, "outside allowed root"};
}

AuditDecision PluginAuditManager::check_open(const std::string& path_str, const std::string& mode)
{
    const bool is_write = mode.find('w') != std::string::npos || mode.find('a') != std::string::npos ||
                          mode.find('+') != std::string::npos || mode.find('x') != std::string::npos;
    return check_path_access(boost::filesystem::path(path_str), is_write);
}

bool PluginAuditManager::request_filesystem_read_permissions(const std::string&              plugin_key,
                                                              const std::vector<std::string>& paths)
{
    if (plugin_key.empty() || paths.empty())
        return true;

    PluginDescriptor descriptor;
    if (!PluginManager::instance().try_get_plugin_descriptor(plugin_key, descriptor) || descriptor.plugin_root.empty())
        return false;

    PluginInstallState state;
    read_install_state(boost::filesystem::path(descriptor.plugin_root), state);

    std::vector<std::string> missing;
    for (const std::string& path : paths) {
        if (std::find(state.permissions.fs_read.begin(), state.permissions.fs_read.end(), path) == state.permissions.fs_read.end())
            missing.push_back(path);
    }
    if (missing.empty())
        return true;

    if (wxTheApp == nullptr || GUI::wxGetApp().is_closing())
        return false;

    auto show_dialog = [&descriptor, &missing]() {
        wxString requested_paths;
        for (const std::string& path : missing)
            requested_paths += wxString::FromUTF8(path.c_str()) + "\n";

        wxMessageDialog dialog(
            nullptr,
            wxString::Format("Plugin \"%s\" requests filesystem read access to:\n%s",
                             wxString::FromUTF8(descriptor.name.c_str()), requested_paths),
            "Plugin permissions",
            wxYES_NO | wxICON_WARNING);
        return dialog.ShowModal() == wxID_YES;
    };

    bool granted = false;
    if (wxIsMainThread()) {
        granted = show_dialog();
    } else {
        auto result = std::make_shared<std::promise<bool>>();
        auto future = result->get_future();
        GUI::wxGetApp().CallAfter([result, descriptor_name = descriptor.name, missing]() {
            wxString requested_paths;
            for (const std::string& path : missing)
                requested_paths += wxString::FromUTF8(path.c_str()) + "\n";

            wxMessageDialog dialog(
                nullptr,
                wxString::Format("Plugin \"%s\" requests filesystem read access to:\n%s",
                                 wxString::FromUTF8(descriptor_name.c_str()), requested_paths),
                "Plugin permissions",
                wxYES_NO | wxICON_WARNING);
            result->set_value(dialog.ShowModal() == wxID_YES);
        });
        granted = future.get();
    }

    if (!granted)
        return false;

    if (state.plugin_name.empty()) {
        state.installed_from    = descriptor.is_cloud_plugin() ? "cloud" : "local";
        state.installed_version = !descriptor.installed_version.empty() ? descriptor.installed_version : descriptor.version;
        state.plugin_name       = descriptor.name;
        state.cloud_uuid        = descriptor.cloud_uuid();
        state.enabled            = true;
    }

    state.permissions.fs_read.insert(state.permissions.fs_read.end(), missing.begin(), missing.end());
    return write_install_state(boost::filesystem::path(descriptor.plugin_root), state);
}

void PluginAuditManager::report_violation(const AuditViolation& violation)
{
    m_last_violation     = violation;
    m_has_last_violation = true;
    m_audit_denial_pending = true;

    BOOST_LOG_TRIVIAL(warning) << "[AUDIT BLOCKED] plugin=" << violation.plugin_key << " event=" << violation.event_name
                               << " path=" << violation.path.string() << " reason=" << violation.reason;
}

bool PluginAuditManager::audit_denial_pending() const { return m_audit_denial_pending; }

void PluginAuditManager::clear_audit_denial() { m_audit_denial_pending = false; }

void PluginAuditManager::clear_last_violation()
{
    m_has_last_violation = false;
    m_last_violation     = AuditViolation{};
}

bool PluginAuditManager::last_violation(AuditViolation& violation) const
{
    if (!m_has_last_violation)
        return false;

    violation = m_last_violation;
    return true;
}

// ---------------------------------------------------------------------------
// The C-level audit hook
// ---------------------------------------------------------------------------

namespace PluginAuditDetail {

PyObject* tuple_item(PyObject* args, Py_ssize_t index)
{
    if (!args || !PyTuple_Check(args) || index < 0 || index >= PyTuple_GET_SIZE(args))
        return nullptr;
    return PyTuple_GET_ITEM(args, index);
}

std::string python_path(PyObject* object)
{
    if (!object)
        return {};

    PyObject* path_object = PyOS_FSPath(object);
    if (!path_object) {
        PyErr_Clear();
        return {};
    }

    const char* path = PyUnicode_Check(path_object) ? PyUnicode_AsUTF8(path_object) : PyBytes_AsString(path_object);
    std::string result = path ? path : "";
    Py_DECREF(path_object);
    if (!path)
        PyErr_Clear();
    return result;
}

void add_filesystem_path(std::vector<boost::filesystem::path>& paths, PyObject* object)
{
    const std::string path = python_path(object);
    if (!path.empty())
        paths.emplace_back(path);
}

std::vector<boost::filesystem::path> filesystem_paths(const std::string& event_name, PyObject* args)
{
    std::vector<boost::filesystem::path> paths;

    if (event_name == "open") {
        add_filesystem_path(paths, tuple_item(args, 0));
    } else if (event_name == "os.rename") {
        add_filesystem_path(paths, tuple_item(args, 0));
        add_filesystem_path(paths, tuple_item(args, 1));
    } else if (event_name == "os.remove") {
        add_filesystem_path(paths, tuple_item(args, 0));
    }

    return paths;
}

bool has_filesystem_permission(const PluginInstallState& state, const boost::filesystem::path& path)
{
    return std::find(state.permissions.fs_read.begin(), state.permissions.fs_read.end(), path.string()) !=
           state.permissions.fs_read.end();
}

bool persist_filesystem_permission(const std::string& plugin_key,
                                   PluginInstallState& state,
                                   const boost::filesystem::path& path)
{
    PluginDescriptor descriptor;
    if (!PluginManager::instance().try_get_plugin_descriptor(plugin_key, descriptor) || descriptor.plugin_root.empty())
        return false;

    // A sandbox plugin may not have a sidecar yet.  Seed the small amount of install metadata
    // needed by the writer so clicking Yes still creates the JSON file.
    if (state.plugin_name.empty()) {
        state.installed_from    = descriptor.is_cloud_plugin() ? "cloud" : "local";
        state.installed_version = !descriptor.installed_version.empty() ? descriptor.installed_version : descriptor.version;
        state.plugin_name       = descriptor.name;
        state.cloud_uuid        = descriptor.cloud_uuid();
        state.enabled            = true;
    }

    const std::string path_string = path.string();
    if (std::find(state.permissions.fs_read.begin(), state.permissions.fs_read.end(), path_string) ==
        state.permissions.fs_read.end())
        state.permissions.fs_read.push_back(path_string);
    return write_install_state(boost::filesystem::path(descriptor.plugin_root), state);
}

// Records a blocked event and raises PermissionError in the calling interpreter.
int report_denied(PluginAuditManager&            mgr,
                  const std::string&             event_name,
                  const boost::filesystem::path& target,
                  const AuditDecision&           decision)
{
    AuditViolation violation;
    violation.plugin_key = mgr.current_plugin();
    violation.event_name = event_name;
    violation.path       = target;
    violation.reason     = decision.reason;
    mgr.report_violation(violation);

    PyErr_SetString(PyExc_PermissionError, "Plugin attempted an audited operation without permission");
    return -1;
}

} // namespace PluginAuditDetail

int PluginAuditManager::audit_hook(const char* event, PyObject* args, void* user_data)
{
    auto* mgr = static_cast<PluginAuditManager*>(user_data);
    if (!mgr)
        return 0;

    std::string event_name(event ? event : "");

    // Verbose logging of every audit event (can be noisy)
    if (mgr->verbose_events) {
        BOOST_LOG_TRIVIAL(debug) << "[AUDIT EVENT] " << event_name;
    }

    if (mgr->current_plugin().empty())
        return 0;

    PluginInstallState state;
    const bool have_install_state = PluginManager::instance().get_install_state(mgr->current_plugin(), state);
    if (!have_install_state) {
        BOOST_LOG_TRIVIAL(warning) << __FUNCTION__ << " Failed to get install state for " << mgr->current_plugin();
    }

    const std::string plugin_name = state.plugin_name.empty() ? mgr->current_plugin() : state.plugin_name;
    const std::vector<boost::filesystem::path> paths = PluginAuditDetail::filesystem_paths(event_name, args);
    for (const auto& path : paths) {
        if (PluginAuditDetail::has_filesystem_permission(state, path))
            continue;

        wxMessageDialog dialog(
            nullptr,
            wxString::Format(_L("Plugin \"%s\" is requesting filesystem access to:\n%s"),
                             wxString::FromUTF8(plugin_name.c_str()),
                             wxString::FromUTF8(path.string().c_str())),
            _L("Plugin permission request"),
            wxYES_NO | wxICON_WARNING);
        if (dialog.ShowModal() == wxID_YES &&
            PluginAuditDetail::persist_filesystem_permission(mgr->current_plugin(), state, path))
            continue;

        return PluginAuditDetail::report_denied(
            *mgr, event_name, path, {false, "filesystem permission required"});
    }

    if (!paths.empty())
        return 0;

    wxMessageDialog dialog(
        nullptr,
        wxString::Format(
            _L("Plugin \"%s\" is requesting permission for the Python audit event \"%s\".\n\n"
                "This operation does not expose a filesystem path to the audit hook."),
            wxString::FromUTF8(plugin_name.c_str()),
            wxString::FromUTF8(event_name.c_str())),
        _L("Plugin permission request"),
        wxYES_NO | wxICON_WARNING);
    if (dialog.ShowModal() == wxID_YES)
        return 0;

    return PluginAuditDetail::report_denied(
        *mgr, event_name, boost::filesystem::path(), {false, "audit permission required"});
}

void PluginAuditManager::install_hook()
{
    if (PySys_AddAuditHook(audit_hook, this) < 0) {
        BOOST_LOG_TRIVIAL(error) << "[AUDIT] Failed to install CPython audit hook";
        return;
    }
    BOOST_LOG_TRIVIAL(info) << "[AUDIT] CPython audit hook installed successfully";

    // data_dir() is the only globally-allowed root during plugin execution.
    // The executable directory and resources directory are intentionally NOT allowed
    // here: plugins must not access outside data_dir() (G-code plugins additionally get
    // the temp G-code folder via a scoped root).
    add_global_allowed_root(data_dir());

    // The user's app config and cloud credentials live directly inside data_dir(), so the
    // root just granted would otherwise expose them to any plugin.  Deny them by name.
    //
    // Seeded here rather than by each secret's owner because install_hook() runs during lazy
    // interpreter init and therefore provably precedes any plugin bytecode, whereas
    // OrcaCloudServiceAgent::set_config_dir runs during networking init — neither strictly
    // precedes the other, and if Orca cloud never initializes an owner-registered token deny
    // would never exist at all.  default_denied_filenames() is the single source of that list
    // (see its comment for why all four config names are denied); the tests seed from it too.
    for (const auto& name : default_denied_filenames())
        add_denied_filename(name);
}

} // namespace Slic3r
