#pragma once

#include "ui/pages/page_loading.h"
#include "lvgl.h"

class PageLoading240x135 : public PageLoadingBase {
public:
    void        create(lv_obj_t* parent) override;
    const char* name() const             override { return "Loading240x135"; }
protected:
    void        _create_dynamic(lv_obj_t* parent) override;
};
