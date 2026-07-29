#ifndef slic3r_TextureDisplacementBakeJob_hpp_
#define slic3r_TextureDisplacementBakeJob_hpp_

#include <functional>
#include <vector>

#include "libslic3r/ObjectID.hpp"
#include "libslic3r/TextureDisplacement.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include "Job.hpp"

namespace Slic3r::GUI {

// Everything process() needs, captured by value on the main thread when the job is queued so the
// worker thread never touches the live Model concurrently with the UI (mirrors how EmbossJob's
// DataBase is captured before process() runs).
struct TextureDisplacementBakeInput
{
    ObjectID                              volume_id;
    indexed_triangle_set                  base_mesh;
    std::vector<TextureDisplacementLayer> layers;
    TextureDisplacementFacetsData         facets_data;
    TextureDisplacementOptions            options;
};

// Bakes a volume's painted texture-displacement layers into real mesh geometry in the background,
// then commits the result on the main thread - mirrors EmbossJob's UpdateJob/update_volume()
// bake-and-commit pattern (see EmbossJob.cpp).
class TextureDisplacementBakeJob : public Job
{
public:
    TextureDisplacementBakeJob(TextureDisplacementBakeInput &&input, std::function<void()> on_finished);

    void process(Ctl &ctl) override;
    void finalize(bool canceled, std::exception_ptr &eptr) override;

private:
    TextureDisplacementBakeInput m_input;
    TriangleMesh                 m_result;
    std::function<void()>        m_on_finished;
};

// Captures `volume`'s current mesh/layers/paint data and queues a TextureDisplacementBakeJob on
// the app's UI job worker. `on_finished` is always called once the job settles (success, failure,
// or cancellation), so the caller can clear its own "bake in progress" UI state. Must be called
// from the main thread.
void queue_texture_displacement_bake(const ModelVolume &volume, std::function<void()> on_finished);

} // namespace Slic3r::GUI

#endif // slic3r_TextureDisplacementBakeJob_hpp_
