// What: Temporary placeholder namespace for the default page-variant concept.
// Why: The framework intends to support page-level variation, but the real page
// replacement mechanism has not been introduced yet.
// Role: Keeps a concrete variant translation unit in the tree during bring-up.
// Benefit: Reserves the architectural seam now, so later variant work can land
// without re-opening the directory structure or naming model.
namespace nm::ui::page_variants::def {

const char* name() {
    return "default";
}

}  // namespace nm::ui::page_variants::def
