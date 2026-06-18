#pragma once

#include "lvgl.h"
#include <Arduino.h>

// ============================================================================
// UIPage — UI page abstract base class
//
//   To add a new UI page:
//     1. Inherit UIPage
//     2. Implement create() / update() / destroy() / name()
//     3. Register in UIManager::init()
//
//   update() is called from the LVGL task thread; no throttling at this layer.
//   Page base classes implement their own throttling in _on_update().
//   NEVER operate LVGL objects outside the LVGL task thread.
// ============================================================================
class UIPage {
public:
    virtual ~UIPage() = default;

    virtual void        create(lv_obj_t* parent) = 0;
    virtual void        destroy()       = 0;
    virtual const char* name() const    = 0;

    void update() { _on_update(); }

protected:
    virtual void _on_update() = 0;
};
