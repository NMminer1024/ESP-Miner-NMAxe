#ifndef _PAGE_DASHBOARD_240_H
#define _PAGE_DASHBOARD_240_H

#include "ui/pages/page_dashboard.h"

class PageDashboard240x135 : public PageDashboardBase {
public:
    void create(lv_obj_t* parent) override;
    const char* name() const override { return "dashboard240x135"; }

protected:
    void _create_dynamic(lv_obj_t* parent) override;
};

#endif
