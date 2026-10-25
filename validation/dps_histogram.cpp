#include "dps_histogram.h"
#include "native_job.h"
#include "../tools/dps_histogram.h"
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <unistd.h>
using namespace fastsim;
using Json = nlohmann::json;
namespace fs = std::filesystem;
static void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string(argv[1]) == "--integration") {
            JobRequest primary; primary.root = fs::canonical(argv[2]); primary.totalFights = 10; primary.runs = 1;
            primary.warmupBatches = 0; primary.allowSoftware = true; primary.wave32 = false;
            primary.histogram = true; primary.rawReports = true;
            NativeJob normal; normal.start(primary); normal.wait(); const auto collected = normal.snapshot();
            check(collected.status == "complete", collected.error.c_str());
            primary.histogram = false;
            NativeJob disabled; disabled.start(primary); disabled.wait(); const auto baseline = disabled.snapshot();
            check(baseline.status == "complete", baseline.error.c_str());
            std::ifstream left(fs::path(collected.outputDirectory) / "native-1.bin", std::ios::binary), right(fs::path(baseline.outputDirectory) / "native-1.bin", std::ios::binary);
            const std::string bytes((std::istreambuf_iterator<char>(left)), {}), expectedBytes((std::istreambuf_iterator<char>(right)), {});
            check(bytes == expectedBytes, "Histogram collection must not alter simulation reports");
            auto rows = collected.summary["runs"][0]["native"]["results"];
            for (size_t i = 0; i < rows.size(); ++i) {
                const auto chart = histogramFromResult(rows[i]);
                const auto& source = rows[i]["histogram"];
                std::vector<uint32_t> expected(256, 0);
                for (uint32_t fight = 0; fight < 2; ++fight) {
                    float damage; std::memcpy(&damage, bytes.data() + (i * 2 + fight) * 64, 4);
                    const auto bin = static_cast<size_t>((damage / 180.0) / source["high"].get<double>() * 256);
                    ++expected.at(bin);
                }
                check(source["bins"] == Json(expected), "Streaming bins must match actual raw reports");
                check(std::accumulate(chart.bins.begin(), chart.bins.end(), 0.0f) == chart.sampleCount, "Lost observed samples");
                rows[i].erase("histogram");
            }
            check(rows == baseline.summary["runs"][0]["native"]["results"], "Histogram must preserve original C summary fields");
            std::cout << "PASS: actual Vulkan histogram counts match every raw report; collection on/off is bit-identical with unchanged summaries\n";
            return 0;
        }
        DpsHistogramCounters counters{}; counters.upper = 1024; counters.scale = .25;
        const std::vector<double> values = {0, 4, 1023.5, 1024, 2048, 1000000, 1e30, 0, 1e30};
        for (const double value : values) histogram_add(&counters, value);
        std::vector<uint32_t> expected(256, 0);
        for (const double value : values) ++expected.at(static_cast<size_t>(value * counters.scale));
        check(std::equal(expected.begin(), expected.end(), counters.bins), "Range expansion must preserve every bin count");
        Json row = {{"candidateId", "x"}, {"sampleCount", values.size()}, {"meanDps", 42}, {"histogram", {{"low", 0}, {"high", counters.upper}, {"bins", expected}}}};
        const auto chart = histogramFromResult(row);
        check(std::accumulate(chart.bins.begin(), chart.bins.end(), 0.0f) == values.size() && chart.bins.size() <= 64, "Display rebinning must preserve full-run counts");
        row["histogram"]["bins"][0] = -1;
        bool invalid = false; try { histogramFromResult(row); } catch (...) { invalid = true; } check(invalid, "Negative bins must reject");
        row["histogram"]["bins"] = expected; row["sampleCount"] = 100;
        invalid = false; try { histogramFromResult(row); } catch (...) { invalid = true; } check(invalid, "Missing histogram samples must reject");
        std::cout << "PASS: streaming range expansion, full-run counts, display rebinning and invalid reports\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
