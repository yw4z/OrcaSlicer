# Texture Displacement - Technical Notes

Branch: `feature/texture_displacement`. This document is a knowledge dump of the whole feature as
it stands: architecture, file map, algorithms, known bugs found and fixed (with root causes worth
remembering), and what's still deferred. Written so a fresh session (or a fresh pair of eyes) can
pick this up without re-deriving everything from scratch.

## What it does

A paint-style gizmo (`GLGizmoTextureDisplacement`) that lets you:
- Paint one or more "layers" onto a model's surface, each a height-map texture with its own
  depth/tiling/rotation/offset/invert/tile-mode/projection-mode/blend-mode.
- Pick a texture from a shipped library (`resources/textures/displacement/`) or import your own
  (saved into `<data_dir>/textures/displacement/`, kept separate so app updates can't clobber it).
- Combine overlapping layers with image-editor-style blend modes (Add/Subtract/Multiply/Divide).
- Preview the true displaced result live, before baking (background job, not on the UI thread).
- Optionally preview via a fast GPU bump-map shader instead (no real geometry movement, just
  shading) for a lighter-weight alternative.
- Bake into real mesh geometry on demand, restricted to the painted area only.
- Subdivide a low-poly model first so there are enough vertices to show fine detail.
- Unwrap a painted patch with a real CGAL LSCM parameterization and view it in a dedicated,
  dockable 2D "UV Editor" pane.

## Architecture

### Data model (per `ModelVolume`)

Each of up to `TEXTURE_DISPLACEMENT_MAX_LAYERS` (8) layers gets its **own independent
`FacetsAnnotation`** paint mask - the exact same `TriangleSelector`/`FacetsAnnotation` machinery
every other paint gizmo (FdmSupports, Seam, MMU, FuzzySkin) already uses, just one full instance
per layer slot instead of one per volume. This is what makes "layered/blended" painting work for
free: the same triangle can be `ENFORCER` in layer 2's mask and layer 5's mask simultaneously, and
at bake/preview time each layer displaces the surface left by the previous one (image-editor-layer
semantics).

### Bake algorithm (`libslic3r/TextureDisplacement.cpp`)

`build_texture_displacement(base_mesh, layers, facets_data)` is **accumulate-then-displace, and
topology-preserving**: the returned mesh has exactly the input's vertices and triangles, in the same
order - only the positions of displaced vertices differ.

1. `its_compactify_vertices()` on a copy of the input. In practice a no-op (it only drops
   *unreferenced* vertices, and preserves the order and indices of the rest). It is there to
   guarantee the index alignment step 3 depends on.
2. Area-weighted vertex normals of the **undisplaced** mesh, computed once. Every layer both
   projects and displaces along these, so a vertex covered by several layers moves along one single
   well-defined direction.
3. For each layer in slot order: deserialize its stored paint mask into a `TriangleSelector` against
   the **base mesh** (never against a previous layer's output), then
   `selector.get_facets_strict(ENFORCER)` → the painted patch. Two facts are exploited:
   - `get_facets_strict()` returns the mesh's **entire** referenced vertex array regardless of which
     state was asked for - only `.indices` is filtered by state. So `get_facets_strict(ENFORCER)`
     and `get_facets_strict(NONE)` share identical vertex indexing, which is what lets boundary
     detection be a plain index check instead of a position-hash lookup.
   - The selector's vertex array *starts with* the mesh's own vertices (extra ones created where a
     brush stroke split a triangle are appended after them), and `get_facets_strict()` emits the
     referenced ones in order. Combined with step 1, **selector vertex index `i` is our vertex `i`**.
     Split vertices live past the end of our array and are simply skipped - they sit on the paint
     boundary anyway (splitting only happens at partial coverage), so they would be pinned regardless.
4. A vertex used by at least one **unpainted** triangle is a boundary vertex - pinned, never
   displaced (its final position is ambiguous, it belongs to both regions). Only vertices used
   exclusively by painted triangles get displaced. This is what keeps bakes seamless with zero
   remeshing/hole-filling at the seam.
5. Per interior vertex: sample the height texture (`sample_layer_height()`, see Projection methods)
   and fold `height * depth_mm * (invert ? -1 : 1)` into that vertex's running total via the layer's
   `TextureBlendMode` (see Blend modes). A `visited` set makes each layer fold in exactly **once**
   per vertex, no matter how many of the patch's triangles share it - otherwise a Multiply/Subtract
   layer would apply two or three times over depending on local triangle fan-out.
6. Finally, move each touched vertex along its (step 2) normal by its accumulated total.

### Blend modes

`TextureBlendMode` {Add, Subtract, Multiply, Divide}, per layer, applied per vertex against the
total accumulated by the layers **below** it (lower slots). The quantity blended is a signed
displacement in **mm**, not a pixel value.

Add/Subtract are self-explanatory. Multiply/Divide are *scaling* operations and so need a unit
convention: they treat the layer's own value as a **factor relative to 1 mm**. That makes `depth_mm`
a gain, and - the property that makes a Multiply layer usable as a mask - a layer with depth 1 mm
sampling a white (1.0) texel multiplies by exactly 1, i.e. leaves the layers below unchanged.
Divide floors its divisor's magnitude at 0.05 - a black texel samples to *exactly* zero, so the
divisor really does hit zero in ordinary use, and an unbounded `1/0` would fling vertices thousands
of mm away and poison the mesh's bounding box (and every plate/print-volume check downstream). The
floor doubles as a cap on how far Divide can amplify the relief beneath it: at most 20×.

The **lowest painted layer ignores its blend mode**: it has nothing beneath it, and Multiply/Divide
against an implicit zero base would annihilate (or blow up) it. Enforced in `build_texture_
displacement()` (the first layer to reach a given vertex always folds in additively) and surfaced in
the UI, which labels that layer "Base layer" instead of offering a control that silently does nothing.

### Projection methods

Four choices per layer (`TextureProjectionMethod`), all funneling through `apply_uv_transform()`
(scale by `1/tiling_scale`, rotate by `rotation_deg`, add `offset`). They are dispatched by
`sample_layer_height()`, which returns a **height**, not a UV - because Triplanar takes three
texture samples per vertex and so has no single UV that represents it.

- **Triplanar** (default) - samples the texture on all three world planes (`(y,z)`, `(x,z)`, `(x,y)`)
  and blends the three by the vertex's own normal raised to `TRIPLANAR_BLEND_SHARPNESS` (4).
  This is the fix for a real, user-reported bug. The previous version *hard-picked* the single axis
  most aligned with the normal, which is discontinuous wherever that dominant axis flips: on a +X
  face the planar coordinate is `(y, z)`, on a −Y face it is `(x, z)`, so at the shared edge `u`
  jumps from `y_edge` to `x_edge`. On a box centred near the origin those two happen to **agree** at
  the (+,+) and (−,−) corners and **differ by the full corner width** at the (+,−) and (−,+) corners
  - which is exactly the "two bad corners, two good ones" symmetry that was observed. A weighted
  blend is continuous across the transition by construction, since the weight of the axis being left
  behind falls smoothly to zero. (Note this removes the hard *seam*; some cross-fade blurring in the
  band right at a 90° edge is inherent to triplanar mapping. A genuinely seam-free wrap around a box
  needs a real unwrap - that is what the LSCM mode is for.)
- **Cylindrical** - wraps around an axis through the patch centroid, axis auto-picked as the world
  axis *least* aligned with the average normal (perpendicular to the outward radial normal, as a
  cylinder's own axis would be). `u = angle * local_radius` (arc length in mm), `v = distance along
  axis`. Approximation, not an exact fit for arbitrary geometry.
- **Spherical** - longitude/latitude around the centroid, scaled by local radius. Same caveat.
- **LSCM** - real UV unwrap via `MeshBoolean::cgal::parameterize_lscm()` (CGAL's
  `Surface_mesh_parameterization` package, LSCM algorithm). Computed **once per patch** (not
  per-vertex like the others - it's a single global least-squares solve), then each vertex looks up
  its precomputed UV. Requires the patch to be a single topological disk (one connected component,
  one boundary loop) - `compute_lscm_uvs()` returns empty and the layer silently falls back to
  Triplanar if not (e.g. multiple disconnected painted islands, or a fully closed patch).
  CGAL's parameterizer needs a mesh with no isolated/unreferenced vertices, but `get_facets_strict()`
  returns the *whole* mesh's vertex array - so there's a compaction step
  (`compact_patch_with_map()`) that builds a clean sub-mesh + an index map back to the original
  (uncompacted) vertex numbering, purely local to this file.
- **ViewProjected** ("From view") - a flat projection along a fixed direction captured from the 3D
  camera, like a slide projector. `capture_view_projection()` takes the camera's right/up axes,
  transforms them into the volume's *local* frame (so the projection rides along if the part is later
  moved), and stores them as `TextureDisplacementLayer::view_project_right/up` (unit vectors, so the
  projected coordinate stays in mm and `tiling_scale` keeps meaning mm). `sample_layer_height()`
  projects `Vec2f(dot(pos, right), dot(pos, up))`. Single-valued per point, so - like LSCM but unlike
  blended Triplanar - the fast preview and UV-check overlay precompute it per vertex
  (`compute_layer_vertex_uvs()`) and drive the shader's `use_vertex_uv` path. Faces angled away from
  the projector smear; that is inherent to view projection, not a bug.

  Two companions to this mode:
  - **Projection frame overlay** (`TextureProjectorFrame`, see below) - a semi-transparent window
    dragged over the 3D view whose border becomes the projection's edge. Applying it stores an exact
    **projective** map in `view_project_matrix`, which supersedes the affine `right`/`up` axes above
    for that layer (`view_project_projective`).
  - **"Project only on visible"** (`select_visible_faces()`) - repaints the layer with exactly the
    facets the camera can see, so the projected area matches the viewpoint the projector was captured
    from. Two tests: a facing test (normal vs. view direction, per triangle - under perspective the
    view direction varies across the model, so it is taken from the eye to each centroid), then
    `MeshRaycaster::get_unobscured_idxs()` on the survivors to drop facets hidden behind other
    geometry, so a concave part's far inner wall is correctly excluded. One ray query per front-facing
    facet, hence click-driven (on the checkbox and on each "Capture current view"), never per frame.
    It **replaces** the layer's paint rather than adding to it - "project onto what I can see" would
    otherwise accumulate every angle the user had ever looked from.

### Manual seams and island cutting

`TextureDisplacementLayer::lscm_seam_edges` - undirected mesh-vertex-index edge pairs the unwrap is
forced to cut along, on top of the dihedral-angle seams. `segment_into_charts()` takes a set of these
(translated from mesh → compacted-patch numbering inside `compute_patch_unwrap()`) and refuses to
union two triangles across a marked edge whatever their angle. Both the unwrap cache key and the
gizmo's `UVEditorState` include the seam list, so marking a seam (which leaves the paint mask
untouched) still forces a re-solve. Like the paint masks, seams are mesh-index-space and so dropped on
any topology change.

Two ways to write to it:
- **Mark seam (manual, #9)** - a "Mark seams" click mode (`m_seam_edit_mode`) that suppresses
  painting. A click raycasts the volume (`m_c->raycaster()->raycasters()[idx]->unproject_on_mesh()`,
  `idx` = the volume's slot among model-part volumes), finds the facet's edge nearest the hit point,
  and toggles it. Marked edges render as a red overlay (`render_seam_overlay()`), pulled toward the
  camera so they read on top. This is the Blender mark-seam workflow.
- **Cut island (auto, #17)** - `cut_island()` takes the selected chart's triangles (back-mapped from
  the unwrap via `source_vertex`), finds their 3D bounding box, and marks every edge that straddles
  the mid-plane perpendicular to the longest axis. The re-unwrap then splits the chart across its
  narrow waist - the "islands might be very long" case. Exposed as the UV pane's **Cut** button.

### UV-check overlays (checker / distortion)

`resources/shaders/{110,140}/texture_displacement_uvcheck.{vs,fs}`, one shader with a `mode` uniform,
drawn over the painted patch (`rebuild_uvcheck_mesh()`/`render_uvcheck_mesh()`, P3N3T2: `normal.x` =
distortion, `tex_coord` = uv), pulled forward with a polygon offset. **Checker** (#13) samples a
procedural checkerboard at the layer's uv (per-vertex for LSCM/ViewProjected, in-shader triplanar
otherwise) - squares that stay square mean low distortion. **Distortion** (#14) colours each triangle
blue→green→red by `log2(uv_area / surface_area)` centred on the patch's *median* stretch (so a
globally-scaled unwrap reads as uniformly ideal and only relative stretch shows), averaged to
vertices. A separate **Show mesh wireframe** toggle (#8) draws the whole volume's triangle edges,
rebuilt only when the vertex count changes (not per stroke).

### Tiling

`DecodedHeightTexture::sample(uv, tile_enabled, tile_method)`. Two tile methods when enabled
(Repeat, MirroredRepeat). **When `tile_enabled` is false, sampling outside `[0,1)` returns `0`
directly** - clamping the *coordinate* into range (what an earlier version did) instead smears the
border row/column of pixels outward to infinity in every direction, which is a real bug that was
reported and fixed (visually: streaky lines radiating out from the painted patch).

### Subdivision — two modes

**Uniform (`subdivide_mesh_uniform()`)** — whole-mesh, 1-to-4 split. Recursive edge-midpoint split with
a shared per-pass midpoint cache (keyed by sorted vertex-index pair) so triangles sharing an edge get
the *same* new vertex - capped at `max_iterations` (default 6). Whole-mesh so it never leaves a
T-junction, at the cost of densifying everywhere. Wired as a "Subdivide steps" slider (**0–5**, 0 =
no subdivision), Apply snaps back to 0. Drops texture-displacement paint (no remap) via the standard
`save_painting()`/`set_mesh()`/`restore_painting()` dance; the other four channels are remapped.

**Adaptive (`subdivide_mesh_adaptive()`)** — refine **only the painted area**, by **Rivara longest-edge
bisection**. This is the algorithm that was "scoped out" originally for fear of the T-junction/crack
problem; it is safe because it is *conformal by construction*. Only **terminal** edges are ever bisected
- an edge that is the longest edge of *every* triangle sharing it - which splits both those triangles
along one shared midpoint at once, so a hanging node is never created. The edge to split for a triangle
that wants refining is found by **longest-edge propagation (LEPP)**: walk to the longest edge of
ever-longer-edged neighbours until a terminal one is reached, and bisect that. Edge length strictly
increases along the path (ties broken by mesh-vertex key, which both sides of an edge compute
identically), so the walk cannot cycle, and Rivara's result is that repeating it refines the original
triangle in a bounded number of bisections. The transition triangles it pulls in just outside the
painted patch are the graded band that makes the size change conformal.

The win: a small decal on a big model no longer quadruples the *whole* model's triangle count.

**Run to completion, worst-first, against a triangle budget.** The refinement loop is not a fixed number
of sweeps: it holds every triangle that is over its criteria in a max-heap keyed by *how many times over*
it is, pops the worst, walks its LEPP, bisects, and re-scores. Edge adjacency (`nb[e]`, the triangle
across each edge) is built **once** and maintained incrementally through each bisection, so the cost
scales with the refined region rather than with the whole model. `max_triangles` is the only bound;
stopping on it leaves a perfectly valid, still-conformal mesh that spent its budget on the largest errors.

This shape replaced a first version that ran a fixed 12 sweeps, each rebuilding a whole-mesh edge map and
bisecting one terminal edge per active triangle. Two failure modes came out of that, and they are worth
remembering because they look like separate bugs and are not: the sweeps were consumed grading the
*coarse surroundings* (whose edges are the longest, so they win every terminal-edge contest), which both
**stopped refinement of the painted patch far short** of the requested detail and left the band outside
it looking wildly over-refined relative to the patch itself.

**It carries the paint forward**, which is what makes it usable (uniform/remesh both drop paint). Because
the refinement is *driven by* the paint, the remap is trivial: `subdivide_mesh_adaptive()` fills an
`out_source[new_tri] = input_tri` map (children inherit their parent), and the gizmo rebuilds each
layer's mask on the new mesh - a new triangle is painted iff its source was fully painted in that
layer. `collect_paint_region()` derives both:
- the union refine-region: **exactly** the original triangles the brush touched, read straight off
  `TriangleSplittingData::triangles_to_split` (`serialize()` records an entry per original triangle that
  is either split - i.e. partially painted, the patch boundary - or carries a non-default state). No
  dilation. An earlier version marked every triangle sharing a *vertex* with the patch, which drags in a
  whole fan of huge unpainted neighbours and then refines *those* down to the resolution floor, since the
  height field the detail test samples is not restricted to the painted area. The conformal closure
  already grades the size change outward on its own; it does not need help.
- the per-layer fully-painted-triangle sets (a `get_facets_strict(ENFORCER)` sub-triangle with all three
  *original* vertex indices == a whole, fully-painted original triangle; a partial stroke's sub-triangles
  always carry a split vertex).

The other four channels still ride the normal `restore_painting()` remap. Covered by a conformality unit
test (`every_edge_used_twice` on a partially-refined cube - an exact crack detector for a closed mesh),
plus tests that the target edge length is actually *reached* and that the budget caps the result without
opening a crack.

Both share the gizmo's Preview/Apply/Done flow; the **"Only painted area (adaptive)"** checkbox picks
the mode, and the adaptive preview follows the paint live (`rebuild_preview()` refreshes the wireframe
while the subdivide preview is open in adaptive mode). The panel shows the previewed triangle count.

**Feature-adaptive (follow texture detail).** A sub-mode of adaptive (the **"Follow texture detail"**
checkbox) that puts triangles where the *displaced surface actually bends*, not evenly. The insight:
a flat region or a linear **ramp** needs no extra vertices (linear interpolation is exact for a ramp);
what needs them is **curvature** - the *second* derivative, not the gradient. So the extra predicate is a
**chord-error** test: sample the combined displacement at the triangle's three edge midpoints *and its
centroid* (sampling the interior is what catches a bump sitting inside a triangle, the blind spot of an
edge-only test) and take the largest departure from the flat triangle's barycentric interpolation. Refine
while that exceeds `chord_tolerance_mm` ("Detail (mm)"). Zero chord error on a ramp ⇒ untouched; high on
a bump/ridge/noise ⇒ refined until captured. Same conformal machinery, so still crack-free. The
per-triangle error is cached and recomputed only for the children of a split.

Four knobs bracket it, and all four matter:
- **"Max edge (mm)"** (`target_edge_length_mm`) is a **baseline that applies in feature mode too**.
  Without it the chord test aliases: a big triangle over a fine pattern can sample four points that all
  land at similar heights, report no error, and stall before refinement ever starts. The baseline
  guarantees a sampling density fine enough for the curvature test to see the texture at all.
- **"Detail (mm)"** is the chord tolerance above.
- **"Min edge (mm)"** is a hard floor under both, and is what guarantees termination across a sharp
  texture *step*, where the error never falls however fine the mesh gets.
- **"Added triangles (k)"** is the budget, passed as `max_triangles` (the model's own triangle count plus
  the slider, so the control still means something on an already-dense model).

The height field is `make_combined_displacement_sampler()` - it mirrors `build_texture_displacement()`'s
per-layer setup (decode, patch centroid, cylinder axis, blend order, "lowest layer folds additively")
but evaluated per point. Two deliberate simplifications, both erring toward *more* detail (safe -
over-refinement is never a crack): every sampleable layer is sampled at every point (no per-point paint
test), and edge-smoothing falloff is ignored. Note the first one is *why* the refine region must not be
dilated - outside the paint the sampler still reports full relief. **LSCM layers are skipped** (no
per-point UV); a purely LSCM stack yields a null sampler and the code falls back to the length baseline
alone. Per-vertex heights are sampled lazily, so a small patch on a huge model never pays for the rest of
it. Covered by unit tests: a Gaussian bump refines densely at its center and leaves flat corners coarse,
a linear ramp produces *zero* extra triangles (the case a gradient criterion would over-refine), and a
flat field still honours the max-edge baseline.

### Fast bump preview (GPU-only, no CPU meshing)

`resources/shaders/{110,140}/texture_displacement_bump.{vs,fs}`, registered as
`"texture_displacement_bump"`. Perturbs the *shading* normal from the height texture's local
gradient instead of moving geometry - active-layer-only, toggled via a "Fast preview (normal map)"
checkbox. Vertex format is `GLModel::Geometry::EVertexLayout::P3N3T2`: `normal.x` carries the
per-vertex paint weight (0/1) and `tex_coord` carries a precomputed texture UV, so it can use
`GLModel` normally instead of needing a hand-rolled VBO/VAO manager. Weight buffer is
rebuilt at the same cadence as the true-displacement preview (stroke-end/slider-release), using the
**live** `TriangleSelector` state (not the flushed model facets), so it doesn't lag by a full model
round-trip.

The perturbed normal is the analytic one for a height field `H = ±depth_mm · h(uv)` displaced along
`N` over any orthonormal surface tangent pair `T`/`B`:

    N' = normalize(N − (dH/da)·T − (dH/db)·B),   a = dot(p,T), b = dot(p,B)

The two slopes have to be genuine **mm-per-mm** derivatives for the preview's apparent depth to
match the bake's - see bug #13.

**Two projection paths (`use_vertex_uv` uniform):**
- **Triplanar (`use_vertex_uv = 0`)** - `uv` and the `T`/`B` axes are both derived in-shader from
  the dominant normal component, mirroring `project_planar()`/`apply_uv_transform()`, and the slope is
  formed analytically. `T`/`B` are the projection's axis-aligned pair, exact only when the face is
  axis-aligned; the shader drops the along-normal component to keep the gradient in the surface. Here
  one `uv` unit is exactly `tiling_scale` mm, so the `1/tiling_scale` gradient factor is right.
- **Precomputed UV (`use_vertex_uv = 1`, used for LSCM)** - `uv` comes per-vertex from the CPU
  (`compute_lscm_uvs(patch, layer)`, so island placement + tiling/rotation/offset are already folded
  in), and the perturbed normal is built with **Mikkelsen's method** ("Bump Mapping Unparametrized
  Surfaces on the GPU"): the surface gradient taken directly from the screen-space derivatives of the
  *sampled height* and position. **This makes no uv→mm scale assumption**, which is essential -
  the first cut used the same global `1/tiling_scale` factor as triplanar and the depth came out
  visibly wrong, because an LSCM map is **conformal, not isometric**: it is globally area-scaled but
  the *local* mm-per-uv varies across the chart. `dFdx(h)` captures the true on-screen rate of change
  however the chart is stretched. **This path is also what makes the fast preview follow the UV
  editor: move an island and its uv - hence its bump - moves with it** (the bump mesh rebuilds on
  drag-end, since `on_island_edited(finished)` → `rebuild_preview()` → `rebuild_bump_preview_mesh()`).
  The branch is uniform (`use_vertex_uv` is a uniform) and the paint weight gates by multiply, so the
  texture derivatives stay well defined. A triangle straddling a seam has a discontinuous uv → the
  `det≈0` guard skips it (a localised preview-only artifact, never in the bake).

Remaining deliberate approximation: the GPU sampler's wrap mode stands in for
`tile_enabled`/`tile_method`, so with tiling *off* the GPU repeats where the CPU returns 0 outside
`[0,1)`.

### On-canvas "Adjust Texture" gizmo

A per-active-layer toggle ("Adjust placement (drag on model)") that disables painting and shows a
flat pan panel (free 2D drag on both axes) plus two arrows along the patch's own U/V axes
(constrained single-axis drag). Anchored to the painted patch's centroid/average-normal
(`compute_layer_paint_anchor()`). Hit-testing is screen-space distance/point-to-segment (not real
3D ray intersection against the handle geometry) - simple and good enough at this handle size.

### Projection frame overlay (ViewProjected)

`src/slic3r/GUI/TextureProjectorFrame.hpp/.cpp` - a semi-transparent, resizable `wxFrame` the user
drags **over the 3D view**, like a slide projector's gate. Whatever the model shows through it is what
the texture is projected onto, and the window's border becomes the hard edge of the displacement.
Press **Apply projection frame** and the gizmo reads the window's rectangle and commits it.

The window is deliberately **dumb**: it owns no placement state and reports nothing continuously. Its
position and size *are* the placement, read on demand at Apply - which is also when the expensive
visible-facet raycast runs. So dragging it is free and nothing recomputes until asked.

Plain 2D (`wxPaintDC`), not a `wxGLCanvas`: a second GL canvas would have to share the app's one real
`wxGLContext`, the cause of bugs #10 and #14 below. It only ever draws a bitmap and a border.

**The projective mapping (`apply_projection_frame()`)**. The frame defines a
**screen-space** rectangle, but the bake samples from a **local-space** position, so the two have to be
reconciled. `view_project_right/up` can only express an *affine* projection - exact under an
orthographic camera, but wrong under perspective, where the near end of a part projects larger than the
far end and no pair of axes reproduces that. So the layer instead stores a full projective map
(`view_project_matrix`, row-major 3×4, `uv = (row0·p̃/row2·p̃, row1·p̃/row2·p̃)`), built like this:

- `K = projection · view · (instance · volume)`, i.e. local → clip, the same product the renderer uses.
  Note `Camera::get_projection_matrix()` is typed `Transform3d` (nominally affine) but its perspective
  form explicitly writes a `(0, 0, −1, 0)` bottom row into the underlying 4×4, so `clip.w = −z_eye` is
  genuinely carried. The build therefore multiplies **`.matrix()` products** (plain `Matrix4d`), never
  `Transform3d` products, which would not compose that row correctly.
- Window coordinates follow `igl::project`'s convention (as `CameraUtils::project` does), with y
  measured downward. Writing `uv = (win − rect_origin) / rect_size` makes u and v affine in
  `ndc = clip.xyz / clip.w`; multiplying through by `clip.w` leaves a plain linear combination of `K`'s
  rows, which is exactly the 3×4 matrix - the perspective divide survives intact.
- `w > 0` is checked rather than divided blindly. A point behind the projector has `w < 0` and divides
  to a plausible-looking but **mirrored** uv - the classic way a projected decal reappears on the back
  of a model. `project_uv_projective()` returns false there and the caller treats it as no height.

The map already includes placement, so `apply_uv_transform()` is **not** applied on top of it - the
window's own position and size are the placement, and the tiling/rotation/offset sliders would shove
the result off the frame the user just aligned. A "Clear" button drops back to the affine path where
those controls mean something again.

Apply also sets `tile_enabled = false`, so `DecodedHeightTexture::sample()` returns 0 outside `[0,1)`
and the border is a hard edge rather than the first seam of an endless repeat, and repaints the layer
via `select_visible_faces(&matrix)` - the frame's uv square clips the selection, which both matches the
paint to the border and keeps the ray queries proportional to the framed area instead of the model.

Owned by the gizmo and **destroyed** (not just hidden) in `on_shutdown()`. Closing it only hides it, so
reopening keeps it where it was left.

### UV Editor pane

`UVEditorCanvas` (`src/slic3r/GUI/UVEditorCanvas.hpp/.cpp`) - a standalone `wxGLCanvas` rendering the
flattened LSCM islands (per-island wireframe + outline + fill) over the height texture (background
quad tiled across the whole unwrap), with mouse pan/zoom. It is wrapped in a **`UVEditorPanel`**
(same file) that adds a button row (Frame / Snap / Average scale) and a status line
along the bottom naming the current gesture and the shortcuts in play. The *panel* is what is
registered as a `wxAuiPaneInfo` pane on `Plater`'s `m_aui_mgr`; `Plater::show_uv_editor(bool)`
shows/hides it (deferred via `CallAfter`, since the gizmo calls it mid-3D-frame), and
`get_uv_editor_canvas()` returns the inner canvas the gizmo talks to.

Deliberately **shares the app's one real `wxGLContext`** (`wxGetApp().init_glcontext(*this)`, the
same call `View3D`/`Preview`/`AssembleView` make) rather than creating an independent context like
`SkipPartCanvas` does elsewhere in this codebase - this is what lets it reuse the already-registered
`"flat"`/`"flat_texture"` shaders and `GLModel` as-is, instead of needing its own shader
compilation/VBO management.

**Geometry is uploaded once, in the unwrap's own (raw, mm) coordinates**, one `GLModel` set per
island; each island is then drawn through its own 2x3 affine (`island_transform_matrix()` composed
with the layer's tiling/rotation/offset) passed as the `flat` shader's `view_model_matrix`. A 
drag updates one matrix per island and touches no vertex
buffer - `on_island_edited(!finished)` calls only `set_island_transforms()`, and the full
`set_islands()` rebuild happens solely when the unwrap itself changes (`unwrap_changed` in
`update_uv_editor()`).

**Gestures** (canvas-owned, reported to the gizmo as incremental deltas via `IslandEditFn`): left-drag
= move, right-drag or **R** = rotate (hold **Shift** to snap to 15° steps - quantised on the
*cumulative* rotation, not each delta, so it doesn't judder, and accumulated incrementally so it
survives crossing ±180°), **S** = scale (R/S modal, click/Enter to confirm, Esc to cancel), wheel =
zoom about the cursor, middle-drag = pan, **Home**/**F** = frame all. Scale writes
`TextureIsland::scale`; "Average scale" (`average_island_scales()`) sets every island to the mean, so
one island scaled by hand can be matched back to its neighbours' texel density. **Snap** (canvas-owned
`m_snap_enabled`, toggled from the toolbar) sticks a dragged island's nearest boundary vertex onto a
neighbouring island's at drag-*end* only - a magnet that re-applies mid-drag is very hard to pull out
of. Toolbar commands the canvas can't service itself (Average scale) are forwarded to the gizmo via
`CommandFn`; view-only ones (Frame, Snap) it handles directly.

## Known limitations / deferred work

- **No `.3mf` serialization** for texture-displacement paint data or texture assets. A background
  agent attempted this in an earlier session, hit its own usage limit mid-edit, and left
  `bbs_3mf.cpp` with an undefined forward-declared function; that partial edit was reverted rather
  than shipped broken. Practical impact: **baked** geometry round-trips fine (it's just an ordinary
  part of the mesh via the existing mesh serialization path) - what does *not* survive a project
  save/reload is any *unbaked* paint stroke and texture layer definition.
- **No remap-across-topology-change** for texture-displacement paint (`ModelObject::split()`, mesh
  boolean ops, Simplify, uniform subdivide, and remesh all drop it via `reset_extra_facets()`). The
  other four paint channels (supported/seam/mmu/fuzzy) do get remapped in these cases. The lone
  exception is **adaptive subdivide**, which carries texture-displacement paint forward itself via its
  source map (see the Subdivision section) - a targeted remap that only works because the operation is
  driven by the paint.
- **Cylindrical/Spherical axis/center are auto-picked heuristically**, not user-controllable - no
  UI to override the auto-detected wrap axis if it picks the "wrong" one for an odd shape.
- **Fast preview covers the active layer only** — and is now the **default** view when the gizmo
  opens (`m_use_bump_preview = true`): it is the instant, no-CPU-meshing preview, so it is the better
  first impression while painting. The exact true-displacement view is one click away in the View row.
- **Displacement resolution is capped by the mesh's own vertex density.** Baking only ever *moves*
  existing vertices (it never inserts any), so a coarse patch cannot show fine texture detail no
  matter how high-resolution the height map is - that is what the subdivision controls are for
  (uniform, adaptive, or feature-adaptive; see the Subdivision section). Since the rewrite the bake is
  topology-preserving, so this is now a hard, explicit property rather than something partly papered
  over by the old per-layer re-meshing.

## File map

**libslic3r (core, no GUI dependency):**
- `src/libslic3r/TextureDisplacement.hpp/.cpp` - data model, bake algorithm, projection methods,
  tiling, subdivision (uniform + adaptive longest-edge bisection). See doc comments throughout,
  they're kept accurate and up to date.
- `src/libslic3r/MeshBoolean.hpp/.cpp` - added `parameterize_lscm()` and `remesh_isotropic()`
  in the `cgal` sub-namespace,
  reusing the existing `CGALMesh`/`_EpicMesh`/conversion-helper infrastructure already there for
  mesh boolean ops. New CGAL includes: `Polygon_mesh_processing/border.h`,
  `Polygon_mesh_processing/connected_components.h`, `Surface_mesh_parameterization/{Error_code,
  LSCM_parameterizer_3, parameterize}.h`. No new dependency - CGAL 5.6.3 is already vendored and
  the `Surface_mesh_parameterization` package headers were already present, just unused before now.
- `src/libslic3r/Model.hpp/.cpp` - the 8 named `FacetsAnnotation` fields + accessor,
  `texture_displacement_layers`, and all the mirrored touch points (see Data model above).

**GUI:**
- `src/slic3r/GUI/Gizmos/GLGizmoTextureDisplacement.hpp/.cpp` - the gizmo. Panel controls: dock/
  undock toggle, brush/face/connected-area selection mode + "select whole model" button, per-layer
  texture picker + depth/tiling/rotation/invert/tile-mode/projection-mode/blend-mode controls,
  "Adjust placement" toggle (on-canvas gizmo), "Fast preview (normal map)" toggle, "Subdivide model"
  button, Add layer/Erase all/Bake.
- `src/slic3r/GUI/TextureLibrary.hpp/.cpp` - scans the shipped + user texture folders, imports an
  arbitrary image into the user folder (converting it to the 8-bit grayscale PNG libslic3r decodes),
  and loads a library file's bytes for a layer. The image→grayscale-PNG conversion lives here, on the
  GUI side, because libslic3r has no image toolkit; both the import path and the "pick a shipped
  texture" path go through the same one function.
- `resources/textures/displacement/*.png` - the 10 shipped height maps (Bricks, Grid, Hexagons,
  Knurl, Noise, Quilt, Studs, Waves, Weave, Wood Grain). All 512×512 8-bit grayscale and **seamless**
  (each is periodic over the full image in both axes, so tiling shows no seam). Generated
  procedurally; the whole `resources/` tree is installed recursively by CMake, so a new folder under
  it ships with no build-system change.
- `src/slic3r/GUI/Jobs/TextureDisplacementBakeJob.hpp/.cpp` - background bake commit.
- `src/slic3r/GUI/Jobs/TextureDisplacementPreviewJob.hpp/.cpp` - background preview compute
  (mirrors the bake job's shape but commits nothing to the Model).
- `src/slic3r/GUI/TextureProjectorFrame.hpp/.cpp` - the semi-transparent projection-frame overlay for
  ViewProjected layers (plain 2D `wxPaintDC`, no GL context - see its section above).
- `src/slic3r/GUI/UVEditorCanvas.hpp/.cpp` - the 2D UV unwrap viewer widget.
- `src/slic3r/GUI/Plater.hpp/.cpp` - `uv_editor_canvas` member, AUI pane registration,
  `get_uv_editor_canvas()`/`show_uv_editor()`.
- `src/slic3r/GUI/GLShadersManager.cpp` - registers `"texture_displacement_bump"`.
- `resources/shaders/{110,140}/texture_displacement_bump.{vs,fs}` - the bump-preview shader.
- `src/slic3r/GUI/Gizmos/GLGizmoPainterBase.hpp` - `PainterGizmoType::TEXTURE_DISPLACEMENT`.
- `src/slic3r/GUI/Gizmos/GLGizmosManager.hpp/.cpp` - `EType::TextureDisplacement` registration.
- `src/slic3r/GUI/ImGuiWrapper.cpp` - the light-mode checkmark-color fix

**Tests:** `tests/libslic3r/test_texture_displacement.cpp` - **run and passing** (7 cases, 116
assertions). Covers `decode_height_texture` round-trip, empty-layer no-op, full-cube uniform
displacement, boundary-vertex pinning on a hand-built fan mesh, and - added with the bake rewrite -
a regression test that a **second layer over the same area actually contributes**, 
a table-driven check of all four blend modes, and that the lowest layer ignores its
blend mode. `BUILD_TESTS` is `OFF` in the checked-in build cache; flip it on to run them:

    cmake -S . -B build -DBUILD_TESTS=ON
    cmake --build build --config Release --target libslic3r_tests -- -m
    ./build/tests/libslic3r/Release/libslic3r_tests.exe "[TextureDisplacement]" --order rand