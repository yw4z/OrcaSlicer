#include "TextureDisplacementPreviewJob.hpp"

#include "slic3r/GUI/I18N.hpp"

namespace Slic3r::GUI {

TextureDisplacementPreviewJob::TextureDisplacementPreviewJob(TextureDisplacementPreviewInput &&input, uint64_t generation,
                                                              std::function<void(indexed_triangle_set, uint64_t)> on_finished)
    : m_input(std::move(input)), m_generation(generation), m_on_finished(std::move(on_finished))
{
}

void TextureDisplacementPreviewJob::process(Ctl &ctl)
{
    ctl.update_status(0, _u8L("Computing texture displacement preview"));

    // Only ever touches m_input (captured by value before this job was queued) and local state -
    // never the live Model - so this is safe to run concurrently with the UI thread.
    m_result = build_texture_displacement(m_input.base_mesh, m_input.layers, m_input.facets_data, m_input.options);
}

void TextureDisplacementPreviewJob::finalize(bool canceled, std::exception_ptr &eptr)
{
    if (canceled || eptr || !m_on_finished)
        return;
    m_on_finished(std::move(m_result), m_generation);
}

} // namespace Slic3r::GUI
