#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
namespace fastsim {
struct DpsHistogram {
    std::string candidateId;
    uint32_t sampleCount = 0;
    double low = 0, high = 0, mean = 0;
    std::vector<float> bins;
};
DpsHistogram histogramFromResult(const nlohmann::json& row);
}
