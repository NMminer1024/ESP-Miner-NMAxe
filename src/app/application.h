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
