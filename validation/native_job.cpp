// Synthetic process-boundary tests. No GPU work or measured performance claims.
#include "native_job.h"
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <unistd.h>
using Json = nlohmann::json;
namespace fs = std::filesystem;
using namespace fastsim;
static void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
static Json read(const fs::path& p) { std::ifstream f(p); return Json::parse(f); }
static void write(const fs::path& p, const Json& value) { std::ofstream f(p); f << value.dump(); }
static int worker(int argc, char** argv) {
    const std::string mode = argv[2];
    fs::path output, work, raw, suitePath, details;
    std::string simulationMode = "fast";
    unsigned fights = 0, batch = 0, warmups = 0;
    bool specialized = true, width = true;
    for (int i = 3; i < argc; ++i) {
        std::string name = argv[i];
        if (name == "--generic") { specialized = false; width = false; }
        else if (name == "--no-subgroup-size") width = false;
        else if (name == "--allow-software" || name == "--no-histogram") {}
        else { check(i + 1 < argc, "Missing mock argument"); std::string value = argv[++i];
            if (name == "--mode") simulationMode = value;
            else if (name == "--details-output") details = value;
            else if (name == "--suite") suitePath = value;
            else if (name == "--output") output = value;
            else if (name == "--work-dir") work = value;
            else if (name == "--raw-output") raw = value;
            else if (name == "--total-fights") fights = std::stoul(value);
            else if (name == "--batch-size") batch = std::stoul(value);
            else if (name == "--warmup-batches") warmups = std::stoul(value);
            else throw std::runtime_error("Unexpected mock argument");
        }
    }
    check(!output.empty() && !work.empty(), "Missing worker paths");
    fs::create_directory(work);
    std::cout << "Pipeline ready on synthetic GPU\n" << std::flush;
    if (mode == "slow") { std::this_thread::sleep_for(std::chrono::seconds(30)); return 2; }
    if (mode == "failure") { std::cerr << "Synthetic worker failure\n"; return 2; }
    const Json suite = read(suitePath.empty() ? fs::path("data/validation/forever_baseline.json") : suitePath);
    Json rows = Json::array();
    for (const auto& scenario : suite["scenarios"]) rows.push_back({{"candidateId", scenario["id"]}, {"status", "complete"}, {"sampleCount", fights / suite["scenarios"].size()}, {"meanDps", 100}, {"sd", 1}, {"standardError", 0.5}, {"maxHeap", 14}, {"totals", {{"total", 12345}}}});
    if (mode == "corrupt") rows[0]["sampleCount"] = 1;
    if (mode == "repeat-mismatch" && output.filename() == "native-2.json") rows[0]["meanDps"] = 101;
    Json result = {{"schemaVersion", 1}, {"classId", "warlock"}, {"status", "complete"}, {"suiteId", suite["id"]}, {"iterations", fights / suite["scenarios"].size()}, {"seed", suite["seed"]}, {"mode", simulationMode}, {"runtime", {{"backend", "Vulkan"}}},
        {"suiteHash", std::string(64, 'a')}, {"inputSnapshotHash", std::string(64, 'b')}, {"kernelHashes", suite["kernelHashes"]},
        {"probe", {{"device", "synthetic GPU"}, {"deviceType", 2}, {"totalFights", fights}, {"warmupBatches", warmups}, {"requiredSubgroupSize", width ? 32 : 0}, {"gpuComputeMs", 40}, {"executeReadbackMs", 45}, {"cDecodeMs", 2}, {"compileMs", 1}, {"warmupMs", 4}, {"draws", (fights + batch - 1) / batch}}},
        {"optimization", {{"variant", specialized ? (width ? "shared-features-wave32" : "shared-features") : "generic"}}},
        {"timing", {{"totalFights", fights}, {"batchSize", batch}, {"wallMs", 70}}}, {"results", rows}};
    if (simulationMode == "detailed") {
        result["details"] = {{"path", details.string()}, {"traceCapacity", 8192}, {"timelineFightIndex", 0}};
        Json analysis = {{"schemaVersion", 1}, {"mode", "detailed"}, {"suiteHash", std::string(64, 'a')}, {"candidates", Json::array()}};
        for (const auto& row : rows) analysis["candidates"].push_back({{"candidateId", row["candidateId"]}, {"fights", row["sampleCount"]}, {"timeline", {{"reportReplay", mode == "bad-details" ? "mismatch" : "bit-identical"}}}});
        write(details, analysis);
    }
    write(output, result);
    if (!raw.empty()) { std::ofstream file(raw, std::ios::binary); std::string bytes(fights * (simulationMode == "detailed" ? 163 : 16) * 4, '\0'); file.write(bytes.data(), bytes.size()); }
    std::cout << "Read back " << fights << '/' << fights << " fights\n";
    fs::remove_all(work); return 0;
}
static void finish(NativeJob& job) {
    for (int i = 0; i < 500 && job.running(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    check(!job.running(), "Worker timeout"); job.wait();
}
int main(int argc, char** argv) {
    try {
        if (argc > 1 && std::string(argv[1]) == "--worker") return worker(argc, argv);
        char temporary[] = "/tmp/fastsim-job-test-XXXXXX"; char* dir = mkdtemp(temporary); check(dir, "Cannot create test directory");
        const fs::path root = dir, self = fs::canonical(argv[0]);
        fs::create_directories(root / "data/validation");
        Json suite = {{"id", "synthetic"}, {"seed", 101}, {"kernelHashes", {{"shader", "hash"}}}, {"scenarios", Json::array()}};
        for (int i = 0; i < 5; ++i) suite["scenarios"].push_back({{"id", "candidate-" + std::to_string(i)}});
        write(root / "data/validation/forever_baseline.json", suite);
        JobRequest request; request.root = root; request.totalFights = 10; request.runs = 2; request.batchSize = 3; request.warmupBatches = 0;
        {
            NativeJob job({self.string(), "--worker", "success"}); job.start(request);
            request.totalFights = 20; finish(job); request.totalFights = 10;
            auto state = job.snapshot(); check(state.status == "complete", state.error.c_str());
            check(state.completedFights == 10 && state.summary["runs"].size() == 2, "Job snapshot/count mismatch");
            check(state.summary["medians"]["gpuSimsPerSecond"] == 250, "Timestamp reduction mismatch");
            auto invalidDevice = state.summary["runs"][0]["native"];
            invalidDevice["probe"]["deviceType"] = "gpu";
            bool deviceRejected = false; try { validateNativeResult(invalidDevice, request, suite); } catch (...) { deviceRejected = true; }
            check(deviceRejected, "Corrupt hardware device type must be rejected");
            check(read(fs::path(state.outputDirectory) / "summary.json")["status"] == "complete", "Missing accepted summary");
            JobRequest duplicate = request; duplicate.outputDirectory = state.outputDirectory;
            bool rejected = false; try { job.start(duplicate); } catch (...) { rejected = true; }
            check(rejected, "Existing evidence must not be overwritten");
        }
        for (const char* mode : {"failure", "corrupt", "repeat-mismatch"}) {
            NativeJob job({self.string(), "--worker", mode}); job.start(request); finish(job);
            auto state = job.snapshot(); check(state.status == "failed" && !state.summary.contains("medians"), "Failed/corrupt results must not publish speed medians");
        }
        {
            NativeJob job({self.string(), "--worker", "slow"}); job.start(request);
            for (int i = 0; i < 200 && job.snapshot().log.empty(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            check(!job.snapshot().log.empty(), "Slow worker did not start");
            bool busy = false; try { job.start(request); } catch (...) { busy = true; }
            check(busy, "Concurrent submissions must reject without disturbing active work");
            job.cancel(); finish(job); auto state = job.snapshot();
            check(state.status == "cancelled" && !state.summary.contains("medians"), "Cancelled job must reject performance summary");
            check(!fs::exists(fs::path(state.outputDirectory) / "work-1") && !fs::exists(fs::path(state.outputDirectory) / "native-1.json"), "Cancelled work must be cleaned up");
        }
        {
            NativeJob job({"/definitely-not-a-fastsim-command"}); job.start(request); finish(job); check(job.snapshot().status == "failed", "Spawn failure must terminate cleanly");
        }
        {
            Json custom = suite; custom["schemaVersion"] = 1; custom["id"] = "editor-warlock-v1";
            custom["scenarios"] = Json::array({{{"id", "edited-candidate"}, {"duration", 180}, {"packed", Json::array({1, 2, 3})}}});
            JobRequest edited = request; edited.suiteSource = custom.dump(); edited.suiteHash = std::string(64, 'a'); edited.candidateSource = "{\"name\":\"immutable draft\"}";
            NativeJob job({self.string(), "--worker", "success"}); job.start(edited);
            edited.suiteSource = "mutated after submit"; finish(job);
            const auto state = job.snapshot(); check(state.status == "complete", state.error.c_str());
            check(read(fs::path(state.outputDirectory) / "suite.json") == custom, "Submitted suite must be immutable");
            check(read(fs::path(state.outputDirectory) / "candidate.json")["name"] == "immutable draft", "Missing source candidate");
            auto result = state.summary["runs"][0]["native"];
            edited.suiteSource = custom.dump(); result["suiteHash"] = std::string(64, 'c');
            bool rejected = false; try { validateNativeResult(result, edited, custom); } catch (...) { rejected = true; }
            check(rejected, "Mismatched editor snapshot hash must reject");
        }
        {
            JobRequest detailed = request; detailed.mode = "detailed"; detailed.runs = 1; detailed.rawReports = true;
            NativeJob job({self.string(), "--worker", "success"}); job.start(detailed); finish(job);
            const auto state = job.snapshot(); check(state.status == "complete", state.error.c_str());
            check(state.summary["mode"] == "detailed" && fs::exists(fs::path(state.outputDirectory) / "details-1.json"), "Missing detailed mode or artifact");
            NativeJob corrupt({self.string(), "--worker", "bad-details"}); corrupt.start(detailed); finish(corrupt);
            check(corrupt.snapshot().status == "failed", "Mismatched trace replay must not publish detailed results");
        }
        fs::remove_all(root);
        std::cout << "PASS: native worker process isolation, immutable snapshots, repeat checks, failure/corruption rejection, cancellation/cleanup, evidence preservation and spawn failure (synthetic process results)\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
