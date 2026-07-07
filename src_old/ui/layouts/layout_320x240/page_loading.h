#pragma once

#include "ui/pages/page_loading.h"
#include "lvgl.h"

class PageLoading320x240 : public PageLoadingBase {
public:
    void        create(lv_obj_t* parent) override;
    const char* name() const             override { return "Loading320x240"; }

protected:
    void        _create_dynamic(lv_obj_t* parent) override;
};
