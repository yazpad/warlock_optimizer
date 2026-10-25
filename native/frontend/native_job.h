#pragma once
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <nlohmann/json.hpp>

namespace fastsim {
// Immutable submission snapshot; all simulation belongs to the native worker.
struct JobRequest {
    std::filesystem::path root;
    std::filesystem::path outputDirectory;
    uint32_t totalFights = 1000000;
    uint32_t batchSize = 65536;
    uint32_t warmupBatches = 2;
    uint32_t runs = 3;
    bool specialized = true;
    bool wave32 = true;
    bool allowSoftware = false;
    bool rawReports = false;
    bool histogram = false;
    std::string mode = "fast";
    std::string presentation = "headless";
    std::string suiteSource; // Empty selects the unchanged frozen benchmark.
    std::string suiteHash;
    std::string candidateSource;
};
struct JobSnapshot {
    std::string status = "idle";
    std::string phase;
    std::string error;
    std::string log;
    std::string outputDirectory;
    uint32_t run = 0;
    uint32_t completedFights = 0;
    uint32_t totalFights = 0;
    nlohmann::json summary;
};
// Process isolation retains the existing engine's failure/cleanup semantics.
// No UI callbacks, graphics APIs, report decoding, or command buffers live here.
class NativeJob {
public:
    explicit NativeJob(std::vector<std::string> runner = {"luajit", "tools/run_native.lua"});
    ~NativeJob();
    NativeJob(const NativeJob&) = delete;
    NativeJob& operator=(const NativeJob&) = delete;
    void start(const JobRequest& request);
    void cancel();
    bool running() const;
    JobSnapshot snapshot() const;
    void wait();
private:
    void execute(JobRequest request);
    void publishLine(const std::string& line);
    void setPhase(const std::string& phase);
    std::vector<std::string> runner_;
    mutable std::mutex mutex_;
    JobSnapshot state_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> cancelRequested_{false};
};
// Validate the process result independently before publishing accepted speeds.
nlohmann::json validateNativeResult(const nlohmann::json& result,
    const JobRequest& request, const nlohmann::json& suite);
}
