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
    uint32_t regretCandidate = 0;
    uint32_t regretDecisions = 128;
    uint32_t regretRounds = 32;
    uint32_t regretSamples = 16;
    uint32_t regretValidation = 1000;
    uint32_t regretDepth = 1;
    uint32_t benchmarkTrials = 8; // Complete matched fight pairs for the online controller.
    uint32_t benchmarkSeedStart = 0; // Choose a fresh evaluation range after tuning.
    float benchmarkLearningRate = 2.0f;
    uint32_t benchmarkAlgo = 0;
    uint32_t evolutionGenerations = 1024, evolutionPopulation = 32;
    float evolutionSigma = 0.1f;
    uint32_t evolutionSeed = 42;
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
    nlohmann::json trainingCurve = nlohmann::json::array();
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
