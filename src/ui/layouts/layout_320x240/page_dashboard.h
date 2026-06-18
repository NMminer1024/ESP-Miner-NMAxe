#ifndef _PAGE_DASHBOARD_320_H
#define _PAGE_DASHBOARD_320_H

#include "ui/pages/page_dashboard.h"

class PageDashboard320x240 : public PageDashboardBase {
public:
    void create(lv_obj_t* parent) override;
    const char* name() const override { return "dashboard320x240"; }

protected:
    void _create_dynamic(lv_obj_t* parent) override;
};

#endif
