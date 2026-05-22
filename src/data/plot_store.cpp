#include "data/plot_store.h"
#include <algorithm>

uint32_t PlotStore::add(const std::string& name) {
    Plot p;
    p.id   = next_id_++;
    p.name = name;
    p.open = true;
    plots_.push_back(std::move(p));
    return plots_.back().id;
}

void PlotStore::remove(uint32_t id) {
    plots_.erase(
        std::remove_if(plots_.begin(), plots_.end(),
                       [id](const Plot& p){ return p.id == id; }),
        plots_.end());
}

Plot* PlotStore::find(uint32_t id) {
    for (auto& p : plots_) {
        if (p.id == id) return &p;
    }
    return nullptr;
}
