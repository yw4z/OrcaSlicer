#include "PluginHost.hpp"
#include "PluginHostBindings.hpp"
#include "PluginHostUi.hpp"
#include <slic3r/plugin/PluginAuditManager.hpp>
#include <slic3r/plugin/PluginDescriptor.hpp>
#include <slic3r/plugin/PluginFsUtils.hpp>
#include <slic3r/plugin/PluginManager.hpp>
#include <slic3r/GUI/GUI_App.hpp>

#include <stdexcept>

namespace Slic3r {

namespace host_bindings {
void register_plugin(pybind11::module_& host)
{
    auto plugin_host = host.def_submodule("plugin", "Plugin host API");

    plugin_host.def(
        "storage",
        []() -> std::string {
            const std::string plugin_key = PluginAuditManager::instance().current_plugin();
            if (plugin_key.empty())
                throw std::runtime_error("plugin.storage() must be called from a plugin callback");

            PluginDescriptor descriptor;
            if (!PluginManager::instance().try_get_plugin_descriptor(plugin_key, descriptor))
                throw std::runtime_error("The current plugin is not registered");

            // plugin_root is populated for installed packages. If it is unavailable, the entry
            // path still identifies the same package directory. This is important for local
            // plugins: their directory is based on the source filename (including its extension),
            // while plugin_key is based on the filename stem.
            const boost::filesystem::path plugin_root = resolve_plugin_root_from_descriptor(descriptor);
            if (!plugin_root.empty())
                return plugin_root.string();

            if (!descriptor.is_cloud_plugin())
                throw std::runtime_error("The current local plugin folder is unavailable");

            if (wxTheApp == nullptr || GUI::wxGetApp().getAgent() == nullptr)
                throw std::runtime_error("Cloud plugin storage is unavailable before networking is initialized");

            const std::string user_id = GUI::wxGetApp().getAgent()->get_user_id();
            if (user_id.empty())
                throw std::runtime_error("Cloud plugin storage is unavailable without a logged-in user");

            if (!is_valid_plugin_id(plugin_key))
                throw std::runtime_error("The current cloud plugin key is not a valid folder name");

            return (boost::filesystem::path(get_cloud_plugin_dir(user_id)) / plugin_key).string();
        },
        "Return the installed folder of the current plugin.");
}
} // namespace host_bindings

void PluginHost::RegisterBindings(pybind11::module_& module)
{
    auto host = module.def_submodule("host", "Host application API");

    // Value types first so the docstring signatures of later registrars
    // resolve to the bound Python names.
    host_bindings::register_geometry(host);
    host_bindings::register_mesh(host);
    host_bindings::register_presets(host);
    host_bindings::register_model(host);
    host_bindings::register_app(host);
    host_bindings::register_plugin(host);

    // UI: native dialogs and interactive HTML windows for plugins.
    PluginHostUi::RegisterBindings(host);

    // Slicing print-graph data model (Print, Layer, Surface, ...).
    host_bindings::register_slicing(host);
}

} // namespace Slic3r
