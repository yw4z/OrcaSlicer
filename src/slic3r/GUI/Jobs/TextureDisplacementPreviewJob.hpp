#ifndef slic3r_TextureDisplacementPreviewJob_hpp_
#define slic3r_TextureDisplacementPreviewJob_hpp_

#include <cstdint>
#include <functional>
#include <vector>

#include "libslic3r/TextureDisplacement.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include "Job.hpp"

namespace Slic3r::GUI {

// Everything process() needs, captured by value on the main thread when the job is queued -
// mirrors TextureDisplacementBakeInput, but a preview never writes back to the Model.
struct TextureDisplacementPreviewInput
{
    indexed_triangle_set                  base_mesh;
    std::vector<TextureDisplacementLayer> layers;
    TextureDisplacementFacetsData         facets_data;
    TextureDisplacementOptions            options;
};

// Computes the true (unbaked) displaced-mesh preview in the background. With several painted
// layers this is real, non-trivial CPU work (PNG sampling, per-layer vertex welding), which used
// to run synchronously on every paint stroke and parameter tweak and made editing feel slow with
// more than one or two layers. Unlike Bake, this never touches the live Model - a preview is
// purely informational, there is nothing to commit.
class TextureDisplacementPreviewJob : public Job
{
public:
    // `generation` is an opaque token the caller controls (typically an incrementing counter):
    // on_finished should only actually be applied by the caller if it still matches the caller's
    // current generation when the job completes, so that a burst of edits queuing several of
    // these jobs in a row can't have an earlier, now-stale result clobber a later one that
    // finishes first.
    TextureDisplacementPreviewJob(TextureDisplacementPreviewInput &&input, uint64_t generation,
                                   std::function<void(indexed_triangle_set, uint64_t)> on_finished);

    void process(Ctl &ctl) override;
    void finalize(bool canceled, std::exception_ptr &eptr) override;

private:
    TextureDisplacementPreviewInput                    m_input;
    uint64_t                                            m_generation;
    indexed_triangle_set                                m_result;
    std::function<void(indexed_triangle_set, uint64_t)> m_on_finished;
};

} // namespace Slic3r::GUI

#endif // slic3r_TextureDisplacementPreviewJob_hpp_
