// What: Market-page base implementation for the new UI framework.
// Why: Market UI will come later, but the page slot and config binding should
// exist now so product navigation already matches the old structure.
// Role: Shows market-related configuration through the shared scaffold.
// Benefit: Future market data integration can stay page-local.
#include "ui/pages/page_market.h"

#include <stdio.h>

namespace nm::ui {

void PageMarketBase::create_market_page(lv_obj_t* parent, const PageScaffoldMetrics& metrics) {
    create_scaffold(parent, metrics);
    set_title("Market");
    set_subtitle("Market config");
    set_footer("TODO(agent): replace with legacy market page");
}

void PageMarketBase::render(const PageContext& context) {
    if (!is_created()) {
        return;
    }

    char line[160] = {};
    set_title("Market");
    set_subtitle("Market config");

    snprintf(line, sizeof(line), "coin %s", context.config.market.display_coin.c_str());
    set_line(0, line);

    snprintf(line, sizeof(line), "watch %s", context.config.market.watchlist.c_str());
    set_line(1, line);

    snprintf(line, sizeof(line), "theme %s", context.config.theme.name.c_str());
    set_line(2, line);

    snprintf(
        line,
        sizeof(line),
        "scheme %s",
        context.config.theme.color_scheme.isEmpty() ? "<default>" : context.config.theme.color_scheme.c_str());
    set_line(3, line);

    set_line(4, "live quotes pending");
    set_footer("market service pending");
}

}  // namespace nm::ui
