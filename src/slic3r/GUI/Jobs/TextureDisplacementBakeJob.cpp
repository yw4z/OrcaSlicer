#include "TextureDisplacementBakeJob.hpp"

#include <algorithm>

#include "libslic3r/Model.hpp"

#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/Utils/UndoRedo.hpp"

namespace Slic3r::GUI {

TextureDisplacementBakeJob::TextureDisplacementBakeJob(TextureDisplacementBakeInput &&input, std::function<void()> on_finished)
    : m_input(std::move(input)), m_on_finished(std::move(on_finished))
{
}

void TextureDisplacementBakeJob::process(Ctl &ctl)
{
    ctl.update_status(0, _u8L("Baking texture displacement"));

    // Only ever touches m_input (captured by value before this job was queued) and local state -
    // never the live Model - so this is safe to run concurrently with the UI thread.
    m_result = TriangleMesh(build_texture_displacement(m_input.base_mesh, m_input.layers, m_input.facets_data,
                                                       m_input.options));
}

void TextureDisplacementBakeJob::finalize(bool canceled, std::exception_ptr &eptr)
{
    struct OnExit
    {
        std::function<void()> fn;
        ~OnExit() { if (fn) fn(); }
    } on_exit{m_on_finished};

    if (canceled || eptr || m_result.empty())
        return;

    Plater *plater = wxGetApp().plater();

    Plater::TakeSnapshot snapshot(plater, _u8L("Bake texture displacement"), UndoRedo::SnapshotType::GizmoAction);

    ModelVolume *volume = get_model_volume(m_input.volume_id, plater->model().objects);
    if (volume == nullptr)
        return;

    volume->set_mesh(std::move(m_result));
    volume->set_new_unique_id();
    volume->calculate_convex_hull();

    // Clear the paint mask of every layer that was actually baked so a repeat bake (or the paint
    // overlay) doesn't act on triangles that no longer represent the same unbaked surface. The
    // texture layer definitions themselves (and paint outside the baked area, if any) are left
    // untouched so the user can keep sculpting with the same textures.
    for (const TextureDisplacementLayer &layer : m_input.layers)
        if (!layer.empty() && layer.slot >= 0 && layer.slot < int(TEXTURE_DISPLACEMENT_MAX_LAYERS))
            volume->texture_displacement_facet(layer.slot).reset();

    ModelObject *object = volume->get_object();
    if (object == nullptr)
        return;

    if (ObjectList *obj_list = wxGetApp().obj_list()) {
        const ModelObjectPtrs &objs = plater->model().objects;
        auto it = std::find(objs.begin(), objs.end(), object);
        if (it != objs.end())
            obj_list->update_info_items(size_t(it - objs.begin()));
    }

    plater->changed_object(*object);
}

void queue_texture_displacement_bake(const ModelVolume &volume, std::function<void()> on_finished)
{
    TextureDisplacementBakeInput input;
    input.volume_id  = volume.id();
    input.base_mesh  = volume.mesh().its;
    input.layers     = volume.texture_displacement_layers;
    input.options    = volume.texture_displacement_options;
    for (int i = 0; i < int(TEXTURE_DISPLACEMENT_MAX_LAYERS); ++i)
        input.facets_data[size_t(i)] = volume.texture_displacement_facet(i).get_data();

    auto &worker = wxGetApp().plater()->get_ui_job_worker();
    queue_job(worker, std::make_unique<TextureDisplacementBakeJob>(std::move(input), std::move(on_finished)));
}

} // namespace Slic3r::GUI
