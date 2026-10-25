#pragma once
#include <filesystem>
#include <vector>
#include <nlohmann/json.hpp>
namespace fastsim {
class DetailedView {
public:
    void clear();
    void load(const std::filesystem::path& path);
    void draw();
private:
    void filter();
    void drawTimeline();
    nlohmann::json data_;
    int candidate_ = 0;
    float from_ = 0, until_ = 0;
    bool damageOnly_ = false;
    char search_[96]{};
    std::vector<size_t> visible_;
};
}
