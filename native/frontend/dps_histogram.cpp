#include "dps_histogram.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace fastsim {
DpsHistogram histogramFromResult(const nlohmann::json& row) {
    const auto& source = row.at("histogram");
    const double low = source.at("low").get<double>(), high = source.at("high").get<double>();
    const auto& bins = source.at("bins");
    const uint32_t count = row.at("sampleCount").get<uint32_t>();
    if (!std::isfinite(low) || !std::isfinite(high) || low != 0 || high <= low || !bins.is_array() || bins.size() != 256 || count < 2)
        throw std::runtime_error("Invalid native histogram range");
    uint64_t sum = 0; size_t first = bins.size(), last = 0;
    for (size_t i = 0; i < bins.size(); ++i) {
        if (!bins[i].is_number_unsigned() && !bins[i].is_number_integer()) throw std::runtime_error("Invalid native histogram count");
        const double value = bins[i].get<double>();
        if (value < 0 || value > count) throw std::runtime_error("Invalid native histogram count");
        sum += bins[i].get<uint32_t>();
        if (value) { first = std::min(first, i); last = i; }
    }
    if (sum != count) throw std::runtime_error("Incomplete native histogram");
    DpsHistogram chart; chart.candidateId = row.at("candidateId").get<std::string>(); chart.sampleCount = count; chart.mean = row.at("meanDps").get<double>();
    const size_t group = (last - first + 64) / 64;
    const size_t size = (last - first + group) / group;
    const double width = (high - low) / bins.size();
    chart.low = low + first * width; chart.high = chart.low + size * group * width;
    chart.bins.assign(size, 0);
    for (size_t i = first; i <= last; ++i) chart.bins[(i - first) / group] += bins[i].get<float>();
    return chart;
}
}
