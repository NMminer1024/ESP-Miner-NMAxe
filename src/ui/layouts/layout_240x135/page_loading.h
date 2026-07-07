// What: 240x135 loading-page concrete class.
// Why: Resolution-specific page classes own the exact widget geometry and
// visuals, even when the page logic is shared by the base class.
// Role: Binds the loading-page base to the 240x135 layout metrics.
// Benefit: The future legacy Gamma loading migration can land here directly.
#pragma once

#include "ui/pages/page_loading.h"

namespace nm::ui {

class PageLoading240x135 : public PageLoadingBase {
public:
    void create(lv_obj_t* parent) override;
    const char* name() const override { return "PageLoading240x135"; }
};

}  // namespace nm::ui
