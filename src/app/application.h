// What: Top-level application orchestrator for one firmware image.
// Why: Even in a BSP-first project, something still needs to define the order of
// serial boot, board bring-up, UI startup, and the main polling loop.
// Role: Owns the high-level runtime sequence above BSP and UI internals.
// Benefit: Startup policy is centralized, making the firmware easier to reason
// about and preventing framework code from leaking into Arduino globals.
#pragma once

namespace nm::bsp {
class Board;
}

namespace nm {

class Application {
public:
    static Application& instance();

    void setup();
    void loop();

    const bsp::Board& board() const;

private:
    Application() = default;

    bsp::Board* _board = nullptr;
    bool _initialized = false;
};

}  // namespace nm
