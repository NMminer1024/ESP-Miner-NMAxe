// What: Temporary layout anchor for the 240x135 UI branch.
// Why: The framework already distinguishes layouts by resolution, even though
// the real page tree for this layout has not been implemented yet.
// Role: Marks the location where 240x135-specific UI composition will live.
// Benefit: Gives the project a stable layout namespace now, making later page
// expansion less disruptive to the surrounding architecture.
namespace nm::ui::layouts::layout_240x135 {

// Temporary layout anchor kept only so the new BSP-first tree has a concrete
// layout namespace during gamma bring-up.
// TODO(agent): replace this stub with real 240x135 page composition.
const char* name() {
    return "layout_240x135";
}

}  // namespace nm::ui::layouts::layout_240x135
