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
    if (mode == "success") std::cout << "Neuro training: {\"generation\":0,\"baselineDps\":100,\"policyDps\":101}\n" << std::flush;
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
            check(state.trainingCurve.size() == 2 && state.trainingCurve[0]["policyDps"] == 101, "Live training progress was not published");
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
        {
            // Exercise policy command wiring and diagnostic identity rejection without a GPU.
            fs::create_directories(root / "tools");
            std::ofstream fake(root / "tools/run_mcts.lua");
            fake << R"lua(
local opts = {}
for i = 1, #arg, 2 do opts[arg[i]] = arg[i+1] end
assert(tonumber(opts['--fights']) == 64)
assert(tonumber(opts['--c-puct']) == 3.5)
local input = assert(io.open('diagnostic.json', 'rb'))
local bytes = input:read('*a'); input:close()
local output = assert(io.open(opts['--output'], 'wb'))
output:write(bytes); output:close()
)lua";
            fake.close();
            Json analysis = {{"schemaVersion", 2}, {"status", "complete"}, {"mode", "policy_benchmark"},
                {"reportReplay", "bit-identical"}, {"candidateId", "candidate-0"}, {"seed", 101}, {"fightIndex", 0},
                {"evaluationScope", "complete_fights"}, {"policyType", "receding_horizon_mcts"},
                {"terminalVerified", true}, {"optimalityGuaranteed", false},
                {"results", Json::array()}, {"timeline", Json::array({Json::object(), Json::object()})},
                {"totalDecisions", 128}, {"totalSearches", 64}, {"completedFightPairs", 64}, {"meanBaselineDps", 100}, {"meanAlgoDps", 90},
                {"dpsDelta", -10}, {"dpsDeltaPct", -10}, {"winRatePct", 0}, {"standardError", 0},
                {"ciLow", -10}, {"ciHigh", -10}, {"confidenceLevel", 0.95},
                {"timing", {{"setupMs", 0}, {"evaluationWallMs", 1}, {"totalWallMs", 1}, {"searchDispatchWallMs", 1},
                    {"searchRolloutFights", 64}, {"replayFights", 128}, {"dispatches", 192}}},
                {"settings", {{"fights", 64}, {"rounds", 16}, {"depth", 3}, {"samples", 8}, {"evaluationStart", 0}, {"cPuct", 3.5}}}};
            for (unsigned pair = 0; pair < 64; ++pair) analysis["results"].push_back({{"fightIndex", pair},
                {"terminalVerified", true}, {"decisions", 2}, {"searches", 1}, {"baselineDps", 100}, {"policyDps", 90}, {"dpsDelta", -10}});
            JobRequest policy = request; policy.mode = "policy_benchmark"; policy.runs = 1;
            policy.benchmarkTrials = 64; policy.benchmarkLearningRate = 3.5f;
            write(root / "diagnostic.json", analysis);
            NativeJob job; job.start(policy); finish(job);
            check(job.snapshot().status == "complete", job.snapshot().error.c_str());
            check(!job.snapshot().summary.contains("medians"), "Policies must not report core throughput");
            for (const char* field : {"mode", "candidateId", "seed", "fightIndex", "reportReplay", "schemaVersion",
                "evaluationScope", "policyType", "terminalVerified", "optimalityGuaranteed"}) {
                Json corrupt = analysis; corrupt[field] = "mismatch";
                write(root / "diagnostic.json", corrupt);
                NativeJob invalid; invalid.start(policy); finish(invalid);
                check(invalid.snapshot().status == "failed", "Wrong diagnostic identity accepted");
            }
            for (const char* kind : {"partial", "duplicate-seed", "wrong-mean", "wrong-budget", "wrong-uncertainty", "wrong-win-fraction", "local-curves"}) {
                Json corrupt = analysis;
                if (std::string(kind) == "partial") corrupt["results"].erase(corrupt["results"].end()-1);
                if (std::string(kind) == "duplicate-seed") corrupt["results"][1]["fightIndex"] = 0;
                if (std::string(kind) == "wrong-mean") corrupt["meanAlgoDps"] = 120;
                if (std::string(kind) == "wrong-budget") corrupt["settings"]["cPuct"] = 1;
                if (std::string(kind) == "wrong-uncertainty") corrupt["standardError"] = 12;
                if (std::string(kind) == "wrong-win-fraction") corrupt["winRatePct"] = 50;
                if (std::string(kind) == "local-curves") corrupt["curves"] = Json::object();
                write(root / "diagnostic.json", corrupt);
                NativeJob invalid; invalid.start(policy); finish(invalid);
                check(invalid.snapshot().status == "failed", "Incomplete/mismatched policy evaluation accepted");
            }
            {
                Json shifted = analysis; shifted["fightIndex"] = 128; shifted["settings"]["evaluationStart"] = 128;
                for (unsigned pair = 0; pair < 64; ++pair) shifted["results"][pair]["fightIndex"] = 128 + pair;
                write(root / "diagnostic.json", shifted);
                JobRequest heldout = policy; heldout.benchmarkSeedStart = 128;
                NativeJob worker; worker.start(heldout); finish(worker);
                check(worker.snapshot().status == "complete", "Held-out seed range rejected");
            }
            for (const auto& invalid : {std::pair<uint32_t, float>{1, 3.5f}, {65, 3.5f}, {64, -1.0f}}) {
                JobRequest bad = policy; bad.benchmarkTrials = invalid.first; bad.benchmarkLearningRate = invalid.second;
                bool rejected = false; try { NativeJob worker; worker.start(bad); } catch (...) { rejected = true; }
                check(rejected, "Invalid policy settings accepted");
            }
            {
                JobRequest overlap = policy; overlap.benchmarkSeedStart = 65535;
                bool rejected = false; try { NativeJob worker; worker.start(overlap); } catch (...) { rejected = true; }
                check(rejected, "Evaluation range overlapping neural training accepted");
            }
            policy.benchmarkAlgo = 2;
            bool rejected = false; try { NativeJob worker; worker.start(policy); } catch (...) { rejected = true; }
            check(rejected, "Unimplemented policy algorithm accepted");
        }
        fs::remove_all(root);
        std::cout << "PASS: native worker process isolation, immutable snapshots, repeat checks, failure/corruption rejection, cancellation/cleanup, evidence preservation and spawn failure (synthetic process results)\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
