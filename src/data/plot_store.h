#pragma once
#include <vector>
#include <cstdint>
#include <string>
#include "data/types.h"

class PlotStore {
public:
    uint32_t  add(const std::string& name);
    void      remove(uint32_t id);
    Plot*     find(uint32_t id);
    std::vector<Plot>& all() { return plots_; }

private:
    std::vector<Plot> plots_;
    uint32_t          next_id_ = 1;
};
