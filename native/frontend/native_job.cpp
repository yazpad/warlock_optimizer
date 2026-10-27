#include "native_job.h"
#include "dps_histogram.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstring>
#include <fstream>
#include <poll.h>
#include <spawn.h>
#include <stdexcept>
#include <system_error>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
namespace fs = std::filesystem;
using Json = nlohmann::json;
namespace fastsim {
namespace {
void require(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
Json readJson(const fs::path& path) {
    std::ifstream file(path);
    require(bool(file), "Cannot read " + path.string());
    return Json::parse(file);
}
void saveJson(const fs::path& path, const Json& value) {
    const auto temporary = path.string() + ".tmp";
    { std::ofstream file(temporary); file << value.dump(2) << '\n'; require(bool(file), "Cannot save " + path.string()); }
    fs::rename(temporary, path);
}
double numeric(const Json& object, const char* key, bool positive = false) {
    require(object.contains(key) && object[key].is_number(), std::string("Missing numeric result: ") + key);
    double value = object[key].get<double>();
    require(std::isfinite(value) && (positive ? value > 0 : value >= 0), std::string("Invalid result: ") + key);
    return value;
}
double diagnosticDelta(const Json& object, const char* key) {
    require(object.contains(key) && object[key].is_number() && std::isfinite(object[key].get<double>()),
        std::string("Invalid diagnostic delta: ") + key);
    return object[key].get<double>();
}
double milliseconds(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
}
Json validateNativeResult(const Json& result, const JobRequest& request, const Json& suite) {
    require(result.at("schemaVersion") == 1 && result.at("classId") == "warlock" && result.at("status") == "complete", "Incomplete or unsupported native result");
    const auto iterations = request.totalFights / suite.at("scenarios").size();
    require(result.at("iterations") == iterations && result.at("seed") == suite.at("seed") && result.at("suiteId") == suite.at("id"), "Native workload identity mismatch");
    require(result.at("mode") == request.mode && result.at("runtime").at("backend") == "Vulkan", "Wrong native mode/backend");
    require(result.at("kernelHashes") == suite.at("kernelHashes"), "Native source snapshot mismatch");
    for (const char* name : {"suiteHash", "inputSnapshotHash"})
        require(result.at(name).is_string() && result.at(name).get<std::string>().size() == 64, std::string("Missing identity: ") + name);
    if (!request.suiteSource.empty()) require(result.at("suiteHash") == request.suiteHash, "Native suite snapshot hash mismatch");
    if (request.mode == "fast") require(!result.contains("details"), "Fast run returned detailed diagnostics");
    else require(result.at("details").at("timelineFightIndex") == 0 && result.at("details").at("traceCapacity") == 8192, "Invalid detailed trace metadata");
    const auto& probe = result.at("probe");
    require(probe.at("totalFights") == request.totalFights && probe.at("warmupBatches") == request.warmupBatches, "Native measured/warmup count mismatch");
    require(probe.at("requiredSubgroupSize") == (request.wave32 ? 32 : 0), "Native subgroup request mismatch");
    if (!request.allowSoftware) require(probe.at("deviceType") == 1 || probe.at("deviceType") == 2 || probe.at("deviceType") == 3, "Hardware run returned a software/unknown device");
    require(probe.at("device").is_string() && !probe.at("device").get<std::string>().empty(), "Missing device identity");
    const std::string variant = request.specialized ? (request.wave32 ? "shared-features-wave32" : "shared-features") : "generic";
    require(result.at("optimization").at("variant") == variant, "Native variant mismatch");
    const auto& rows = result.at("results");
    require(rows.size() == suite.at("scenarios").size(), "Native candidate count mismatch");
    uint64_t samples = 0;
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& row = rows[i];
        require(row.at("candidateId") == suite.at("scenarios")[i].at("id") && row.at("status") == "complete" && row.at("sampleCount") == iterations, "Native candidate identity/completion mismatch");
        if (request.histogram) (void)histogramFromResult(row);
        else require(!row.contains("histogram"), "Unexpected native histogram collection");
        numeric(row, "meanDps"); numeric(row, "sd"); numeric(row, "standardError");
        require(numeric(row, "maxHeap") <= 40, "Native heap capacity exceeded");
        numeric(row.at("totals"), "total");
        samples += row.at("sampleCount").get<uint64_t>();
    }
    require(samples == request.totalFights, "Incomplete native sample total");
    const auto& timing = result.at("timing");
    require(timing.at("totalFights") == request.totalFights && timing.at("batchSize") == request.batchSize, "Native batch/workload mismatch");
    const double gpu = numeric(probe, "gpuComputeMs", true);
    const double wall = numeric(timing, "wallMs", true);
    const double execute = numeric(probe, "executeReadbackMs", true);
    const double decode = numeric(probe, "cDecodeMs");
    numeric(probe, "compileMs"); numeric(probe, "warmupMs");
    require(probe.at("draws") == (request.totalFights + request.batchSize - 1) / request.batchSize, "Native dispatch count mismatch");
    return {{"gpuComputeMs", gpu}, {"nativeWallMs", wall}, {"executeReadbackMs", execute}, {"cDecodeMs", decode},
        {"gpuSimsPerSecond", request.totalFights * 1000.0 / gpu},
        {"nativeSimsPerSecond", request.totalFights * 1000.0 / wall},
        {"steadySimsPerSecond", request.totalFights * 1000.0 / (execute + decode)}};
}
NativeJob::NativeJob(std::vector<std::string> runner) : runner_(std::move(runner)) { require(!runner_.empty(), "Missing native runner command"); }
NativeJob::~NativeJob() { cancel(); wait(); }
bool NativeJob::running() const { return running_.load(); }
JobSnapshot NativeJob::snapshot() const { std::lock_guard<std::mutex> lock(mutex_); return state_; }
void NativeJob::cancel() { cancelRequested_.store(true); }
void NativeJob::wait() { if (thread_.joinable()) thread_.join(); }
void NativeJob::setPhase(const std::string& phase) { std::lock_guard<std::mutex> lock(mutex_); state_.phase = phase; }
void NativeJob::publishLine(const std::string& line) {
    std::lock_guard<std::mutex> lock(mutex_);
    state_.log += line + '\n';
    if (state_.log.size() > 32768) state_.log.erase(0, state_.log.size() - 32768);
    if (line.rfind("Neuro training: ", 0) == 0) {
        auto point = nlohmann::json::parse(line.substr(16), nullptr, false);
        if (!point.is_discarded() && point.is_object() && point.contains("generation") && point.contains("baselineDps") && point.contains("policyDps")) {
            state_.trainingCurve.push_back(point);
            const char* label = point.contains("policyLoss") ? "PPO / iteration " : "Neuroevolution / generation ";
            state_.phase = label + point.at("generation").dump();
        }
    }
    unsigned completed = 0, total = 0;
    if (std::sscanf(line.c_str(), "Read back %u/%u fights", &completed, &total) == 2 && total == state_.totalFights && completed <= total) {
        state_.completedFights = completed; state_.phase = "Native GPU execution / C aggregation";
    } else if (line.find("Policy evaluation:") != std::string::npos) state_.phase = line;
    else if (line.find("Regret analysis:") != std::string::npos) state_.phase = line;
    else if (line.find("Detailed analysis:") != std::string::npos) state_.phase = "Detailed trace and analysis";
    else if (line.find("Warmup complete:") != std::string::npos) state_.phase = "Measured native dispatches";
    else if (line.find("Pipeline ready") != std::string::npos) state_.phase = "Native pipeline ready / discarded warmups";
}
void NativeJob::start(const JobRequest& input) {
    require(!running(), "A native job is already running");
    wait();
    JobRequest request = input;
    request.root = fs::canonical(request.root);
    const Json suite = request.suiteSource.empty() ? readJson(request.root / "data/validation/forever_baseline.json") : Json::parse(request.suiteSource);
    if (!request.suiteSource.empty()) {
        require(request.suiteSource.size() <= 1048576 && request.suiteHash.size() == 64, "Invalid suite snapshot identity");
        require(suite.at("schemaVersion") == 1 && suite.at("id") == "editor-warlock-v1" && suite.at("scenarios").size() == 1, "Unsupported editor suite");
        require(suite.at("kernelHashes") == readJson(request.root / "data/validation/forever_baseline.json").at("kernelHashes"), "Editor kernel identity mismatch");
    }
    const size_t candidates = suite.at("scenarios").size();
    require(candidates, "Empty candidate suite");
    if (request.mode != "policy_benchmark") require(request.totalFights % candidates == 0 && request.totalFights / candidates >= 2 && request.totalFights / candidates <= 1048576 && request.totalFights <= 16777216, "Invalid fight count for selected candidates");
    require(request.mode == "fast" || request.mode == "detailed" || request.mode == "regret" || request.mode == "policy_benchmark", "Invalid simulation mode");
    if (request.mode == "policy_benchmark") {
        require(request.benchmarkAlgo <= 2 && request.evolutionGenerations >= 1 && request.evolutionGenerations <= 16384 &&
            request.evolutionPopulation >= 2 && request.evolutionPopulation <= 64 && request.evolutionSeed >= 1 && request.evolutionSeed <= 2147483646 &&
            std::isfinite(request.evolutionSigma) && request.evolutionSigma > 0 && request.evolutionSigma <= 2 && request.benchmarkTrials >= 2 && request.benchmarkTrials <= (request.benchmarkAlgo >= 1 ? 8192u : 64u) && request.benchmarkSeedStart <= 65535 &&
            request.benchmarkSeedStart + request.benchmarkTrials <= 65536 &&
            std::isfinite(request.benchmarkLearningRate) && request.benchmarkLearningRate >= 0 && request.benchmarkLearningRate <= 100,
            "Invalid policy benchmark settings");
        request.totalFights = request.benchmarkTrials * 2; // Actual evaluated fights, excluding search/replay work.
    }
    if (request.mode == "regret" || request.mode == "policy_benchmark") {
        require(request.runs == 1 && !request.rawReports, "Regret/Policy analysis is a single diagnostic job without raw reports");
        require(request.regretCandidate < candidates && request.regretDecisions >= 1 && request.regretDecisions <= 128 &&
            request.regretRounds >= 1 && request.regretRounds <= 256 && request.regretSamples >= 2 && request.regretSamples <= 256 &&
            request.regretValidation >= 2 && request.regretValidation <= 4096 && request.regretDepth >= 1 && request.regretDepth <= 4,
            "Invalid regret search budget");
    }
    if (!request.regretTraceSource.empty()) {
        require(request.mode == "regret" && request.regretTraceSource.size() <= 1048576, "Invalid trace request");
        const auto trace = Json::parse(request.regretTraceSource);
        require(trace.at("decision").is_number_unsigned() && trace.at("decision").get<unsigned>() < 1024 &&
            trace.at("suiteHash").is_string() && trace.at("takenPlan").is_string() && trace.at("alternativePlan").is_string(), "Invalid trace selection");
    }
    require(request.batchSize >= 1 && request.batchSize <= 65536 && request.warmupBatches <= 32 && request.runs >= 1 && request.runs <= 10, "Invalid batch/warmup/run settings");
    require(!request.wave32 || request.specialized, "Current native CLI requires specialization for wave32");
    require(request.presentation == "headless" || request.presentation == "rendering-paused" || request.presentation == "visible-10hz", "Invalid presentation label");
    if (request.outputDirectory.empty()) request.outputDirectory = request.root / "artifacts/imgui" / (std::to_string(getpid()) + "-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (request.outputDirectory.is_relative()) request.outputDirectory = request.root / request.outputDirectory;
    fs::create_directories(request.outputDirectory.parent_path());
    require(fs::create_directory(request.outputDirectory), "Output directory already exists; use a fresh directory");
    { std::lock_guard<std::mutex> lock(mutex_); state_ = {}; state_.status = "running"; state_.phase = "Preparing native worker"; state_.totalFights = request.totalFights; state_.outputDirectory = request.outputDirectory.string(); }
    cancelRequested_.store(false); running_.store(true);
    try { thread_ = std::thread(&NativeJob::execute, this, std::move(request)); }
    catch (...) { running_.store(false); std::lock_guard<std::mutex> lock(mutex_); state_.status = "failed"; throw; }
}
void NativeJob::execute(JobRequest request) {
    Json summary = {{"schemaVersion", 1}, {"status", "running"}, {"frontend", "Dear ImGui native process client"},
        {"computeBackend", (request.mode == "regret" || request.mode == "policy_benchmark") ? "Separate GPU regret/policy diagnostic adapter" : "unchanged tools/run_native.lua + tools/vulkan_probe.c"},
        {"presentation", request.presentation}, {"mode", request.mode}, {"totalFights", request.totalFights}, {"batchSize", request.batchSize},
        {"warmupBatches", request.warmupBatches}, {"requestedRuns", request.runs}, {"rawReports", request.rawReports}, {"histogram", request.histogram}, {"runs", Json::array()},
        {"timingScope", "GPU/native/steady metrics come from native engine. Client wall includes process startup and JSON loading. UI rendering mode is labeled separately; rendering performance is not inferred from headless results."},
        {"validation", "diagnostic-only; hardware GUI performance gates and accepted Forever parity remain separate"}};
    pid_t child = -1;
    int readFd = -1;
    fs::path activeOutput, activeRaw, activeDetails, workDirectory;
    try {
        const Json suite = request.suiteSource.empty() ? readJson(request.root / "data/validation/forever_baseline.json") : Json::parse(request.suiteSource);
        if (!request.suiteSource.empty()) {
            std::ofstream file(request.outputDirectory / "suite.json");
            file << request.suiteSource; require(bool(file), "Cannot save suite snapshot");
            summary["suiteHash"] = request.suiteHash;
            if (!request.candidateSource.empty()) { std::ofstream candidate(request.outputDirectory / "candidate.json"); candidate << request.candidateSource; require(bool(candidate), "Cannot save candidate snapshot"); }
        }
        Json previous;
        for (uint32_t index = 0; index < request.runs; ++index) {
            require(!cancelRequested_.load(), "Cancelled");
            { std::lock_guard<std::mutex> lock(mutex_); state_.run = index + 1; state_.completedFights = 0; }
            setPhase("Preparing native worker / shader cache");
            activeOutput = request.outputDirectory / ("native-" + std::to_string(index + 1) + ".json");
            activeRaw = request.outputDirectory / ("native-" + std::to_string(index + 1) + ".bin");
            if (request.mode == "detailed") activeDetails = request.outputDirectory / ("details-" + std::to_string(index + 1) + ".json");
            workDirectory = request.outputDirectory / ("work-" + std::to_string(index + 1));
            std::vector<std::string> command;
            if (request.mode == "policy_benchmark") {
                command = {"luajit", "tools/run_mcts.lua",
                    "--candidate", std::to_string(request.regretCandidate),
                    "--fights", std::to_string(request.benchmarkTrials),
                    "--evaluation-start", std::to_string(request.benchmarkSeedStart),
                    "--rounds", "16",
                    "--depth", "3",
                    "--samples", "8",
                    "--c-puct", std::to_string(request.benchmarkLearningRate),
                    "--apl-weight", "0.70",
                    "--algorithm", request.benchmarkAlgo == 2 ? "ppo" : (request.benchmarkAlgo == 1 ? "neuroevolution" : "mcts"),
                    "--generations", std::to_string(request.evolutionGenerations),
                    "--population", std::to_string(request.evolutionPopulation),
                    "--mutation-sigma", std::to_string(request.evolutionSigma),
                    "--evolution-seed", std::to_string(request.evolutionSeed)};
            } else if (request.mode == "regret") {
                command = {"luajit", "tools/run_regret.lua",
                    "--regret-candidate", std::to_string(request.regretCandidate),
                    "--regret-decisions", std::to_string(request.regretDecisions),
                    "--regret-rounds", std::to_string(request.regretRounds),
                    "--regret-samples", std::to_string(request.regretSamples),
                    "--regret-validation", std::to_string(request.regretValidation),
                    "--regret-depth", std::to_string(request.regretDepth)};
                if (!request.regretTraceSource.empty()) {
                    const auto path = request.outputDirectory / "trace-request.json";
                    std::ofstream file(path); file << request.regretTraceSource;
                    require(bool(file), "Cannot save trace request"); file.close();
                    command.push_back("--trace-request"); command.push_back(path.string());
                }
            } else {
                command = runner_;
            }
            for (const auto& value : std::vector<std::string>{"--total-fights", std::to_string(request.totalFights), "--batch-size", std::to_string(request.batchSize), "--warmup-batches", std::to_string(request.warmupBatches), "--output", activeOutput.string(), "--work-dir", workDirectory.string()}) command.push_back(value);
            if (request.mode == "detailed") { command.push_back("--mode"); command.push_back("detailed"); command.push_back("--details-output"); command.push_back(activeDetails.string()); }
            if (!request.suiteSource.empty()) { command.push_back("--suite"); command.push_back((request.outputDirectory / "suite.json").string()); }
            if (!request.specialized) command.push_back("--generic");
            else if (!request.wave32) command.push_back("--no-subgroup-size");
            if (request.allowSoftware) command.push_back("--allow-software");
            if (!request.histogram) command.push_back("--no-histogram");
            if (request.rawReports) { command.push_back("--raw-output"); command.push_back(activeRaw.string()); }
            std::vector<char*> argv; for (auto& argument : command) argv.push_back(argument.data()); argv.push_back(nullptr);
            int pipeFd[2]; require(pipe2(pipeFd, O_CLOEXEC) == 0, "Cannot create native worker pipe");
            posix_spawn_file_actions_t actions;
            posix_spawnattr_t attributes;
            posix_spawn_file_actions_init(&actions); posix_spawnattr_init(&attributes);
            posix_spawn_file_actions_addchdir_np(&actions, request.root.c_str());
            posix_spawn_file_actions_adddup2(&actions, pipeFd[1], STDOUT_FILENO);
            posix_spawn_file_actions_adddup2(&actions, pipeFd[1], STDERR_FILENO);
            posix_spawn_file_actions_addclose(&actions, pipeFd[0]);
            posix_spawn_file_actions_addclose(&actions, pipeFd[1]);
            posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
            posix_spawnattr_setpgroup(&attributes, 0);
            auto started = std::chrono::steady_clock::now();
            const int spawnError = posix_spawnp(&child, command[0].c_str(), &actions, &attributes, argv.data(), environ);
            posix_spawn_file_actions_destroy(&actions); posix_spawnattr_destroy(&attributes); close(pipeFd[1]);
            if (spawnError) { close(pipeFd[0]); child = -1; throw std::runtime_error("Cannot launch native worker: " + std::string(std::strerror(spawnError))); }
            readFd = pipeFd[0];
            fcntl(readFd, F_SETFL, fcntl(readFd, F_GETFL) | O_NONBLOCK);
            bool exited = false, eof = false, terminateSent = false;
            int status = 0;
            auto termination = started;
            std::string pending;
            while (!exited || !eof) {
                if (cancelRequested_.load() && !terminateSent) { kill(-child, SIGTERM); terminateSent = true; termination = std::chrono::steady_clock::now(); }
                if (terminateSent && milliseconds(termination) > 2000) kill(-child, SIGKILL);
                pollfd descriptor{readFd, POLLIN | POLLHUP, 0};
                const int polled = poll(&descriptor, 1, 50);
                require(polled >= 0 || errno == EINTR, "Native worker pipe polling failed");
                char bytes[4096];
                while (true) {
                    ssize_t count = read(readFd, bytes, sizeof(bytes));
                    if (count > 0) {
                        pending.append(bytes, static_cast<size_t>(count));
                        size_t end;
                        while ((end = pending.find('\n')) != std::string::npos) { publishLine(pending.substr(0, end)); pending.erase(0, end + 1); }
                        if (pending.size() > 32768) { publishLine(pending.substr(0, 32768)); pending.clear(); }
                    } else { if (count == 0) eof = true; else require(errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR, "Cannot read native worker output"); break; }
                }
                if (!exited) {
                    pid_t waited = waitpid(child, &status, WNOHANG);
                    require(waited >= 0 || errno == EINTR, "Cannot wait for native worker");
                    if (waited == child) { exited = true; if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) kill(-child, SIGKILL); }
                }
            }
            if (!pending.empty()) publishLine(pending);
            close(readFd); readFd = -1; child = -1;
            require(!cancelRequested_.load(), "Cancelled");
            require(WIFEXITED(status) && WEXITSTATUS(status) == 0, "Native worker failed; inspect the captured log");
            Json result = readJson(activeOutput);
            if (request.mode == "regret" || request.mode == "policy_benchmark") {
                require(result.at("schemaVersion") == (request.mode == "policy_benchmark" ? 2 : 1) && result.at("status") == "complete" &&
                    result.at("mode") == request.mode && result.at("reportReplay") == "bit-identical" &&
                    result.at("candidateId") == suite.at("scenarios")[request.regretCandidate].at("id") &&
                    result.at("seed") == suite.at("seed") &&
                    result.at("fightIndex") == (request.mode == "policy_benchmark" ? request.benchmarkSeedStart : 0), "Invalid diagnostic result identity");
                if (!request.suiteSource.empty()) require(result.at("suiteHash") == request.suiteHash, "Diagnostic suite snapshot mismatch");
                if (request.mode == "regret") {
                    require(result.at("rows").is_array() && result.at("rows").size() <= request.regretDecisions, "Invalid regret rows");
                    if (!request.regretTraceSource.empty()) {
                        const auto expected = Json::parse(request.regretTraceSource);
                        const auto& trace = result.at("trace");
                        require(trace.at("schemaVersion") == 2 && trace.at("drivers").is_array() && trace.at("decision") == expected.at("decision") && trace.at("takenPlan") == expected.at("takenPlan") &&
                            trace.at("alternativePlan") == expected.at("alternativePlan") && trace.at("samples") == 64 &&
                            trace.at("instrumentationVerified") == true && result.at("terminalVerified") == true &&
                            result.at("suiteHash") == expected.at("suiteHash"), "Trace identity or replay mismatch");
                        diagnosticDelta(trace, "dpsDelta"); numeric(trace, "standardError");
                        require(trace.at("outcomes").is_array() && trace.at("outcomes").size() == 64 &&
                            trace.at("example").at("taken").at("timeline").is_array() &&
                            trace.at("example").at("alternative").at("timeline").is_array(), "Incomplete paired trace");
                    } else require(!result.contains("trace"), "Unexpected trace result");
                    for (const auto& row : result.at("rows")) {
                        numeric(row, "time"); numeric(row, "dpsDelta"); numeric(row, "standardError");
                        require(row.at("samples") == request.regretValidation, "Regret sample count mismatch");
                    }
                } else {
                    const char* expectedPolicyType = request.benchmarkAlgo == 2 ? "ppo_mlp" : (request.benchmarkAlgo == 1 ? "neuroevolution_mlp" : "receding_horizon_mcts");
                    require(result.at("evaluationScope") == "complete_fights" && result.at("policyType") == expectedPolicyType &&
                        result.at("terminalVerified") == true && result.at("optimalityGuaranteed") == false &&
                        result.at("results").is_array() && result.at("results").size() == request.benchmarkTrials &&
                        result.at("completedFightPairs") == request.benchmarkTrials, "Invalid complete-policy result scope/count");
                    for (const char* key : {"meanBaselineDps", "meanAlgoDps", "winRatePct", "standardError"}) numeric(result, key);
                    diagnosticDelta(result, "dpsDelta"); diagnosticDelta(result, "dpsDeltaPct");
                    const double ciLow = diagnosticDelta(result, "ciLow"), ciHigh = diagnosticDelta(result, "ciHigh");
                    require(ciLow <= result.at("dpsDelta").get<double>() && ciHigh >= result.at("dpsDelta").get<double>() &&
                        result.at("winRatePct").get<double>() <= 100 && result.at("confidenceLevel") == 0.95, "Invalid policy uncertainty");
                    const auto& settings = result.at("settings");
                    require(settings.at("fights") == request.benchmarkTrials && settings.at("rounds") == 16 &&
                        settings.at("depth") == 3 && settings.at("samples") == 8 && settings.at("evaluationStart") == request.benchmarkSeedStart &&
                        std::abs(settings.at("cPuct").get<double>() - request.benchmarkLearningRate) <= 1e-5,
                        "Invalid policy controller settings");
                    if (request.benchmarkAlgo >= 1) {
                        require(settings.at("generations") == request.evolutionGenerations && settings.at("population") == request.evolutionPopulation &&
                            settings.at("evolutionSeed") == request.evolutionSeed && std::abs(settings.at("mutationSigma").get<double>() - request.evolutionSigma) < 1e-5 &&
                            result.at("totalSearches") == 0 && result.at("actionSelection") == "masked_mlp_argmax" &&
                            result.at("inferenceBackend") == "batched_gpu_full_fights" &&
                            settings.at("trainingFights") == 1000 && result.at("trainingSeeds").size() == 1000 &&
                            result.at("trainingCurve").size() == request.evolutionGenerations + 1 && result.at("network").at("version") == 2 &&
                            result.at("network").at("inputs") == 75 && result.at("network").at("hidden") == 12 &&
                            result.at("network").at("outputs") == 26 && result.at("network").at("weights").size() == 1250,
                            "Invalid neural policy result");
                        uint32_t generation = 0;
                        for (const auto& point : result.at("trainingCurve")) {
                            require(point.at("generation") == generation++, "Invalid training generation");
                            numeric(point, "baselineDps"); numeric(point, "policyDps");
                        }
                    }
                    uint32_t pair = 0;
                    double baselineMean = 0, policyMean = 0, deltaMean = 0, deltaM2 = 0;
                    uint64_t decisions = 0, searches = 0;
                    uint32_t wins = 0;
                    for (const auto& row : result.at("results")) {
                        require(row.at("fightIndex") == request.benchmarkSeedStart + pair++ && row.at("terminalVerified") == true &&
                            row.at("decisions").is_number_unsigned() && row.at("searches").is_number_unsigned() &&
                            row.at("searches").get<uint32_t>() <= row.at("decisions").get<uint32_t>(), "Invalid complete fight pair");
                        const double baseline = numeric(row, "baselineDps"), policy = numeric(row, "policyDps");
                        require(std::abs(diagnosticDelta(row, "dpsDelta") - (policy - baseline)) < 1e-6, "Invalid paired DPS delta");
                        baselineMean += baseline / request.benchmarkTrials; policyMean += policy / request.benchmarkTrials;
                        const double delta = policy - baseline, change = delta - deltaMean;
                        deltaMean += change / pair; deltaM2 += change * (delta - deltaMean);
                        if (delta > 0) ++wins;
                        decisions += row.at("decisions").get<uint32_t>(); searches += row.at("searches").get<uint32_t>();
                    }
                    require(std::abs(baselineMean - result.at("meanBaselineDps").get<double>()) < 1e-6 &&
                        std::abs(policyMean - result.at("meanAlgoDps").get<double>()) < 1e-6 &&
                        std::abs(policyMean - baselineMean - result.at("dpsDelta").get<double>()) < 1e-6,
                        "Policy aggregate does not match complete fight pairs");
                    const double standardError = std::sqrt(std::max(0.0, deltaM2) / (request.benchmarkTrials - 1) / request.benchmarkTrials);
                    require(std::abs(result.at("standardError").get<double>() - standardError) < 1e-6 &&
                        std::abs(result.at("winRatePct").get<double>() - 100.0 * wins / request.benchmarkTrials) < 1e-6 &&
                        result.at("totalDecisions") == decisions && result.at("totalSearches") == searches,
                        "Policy statistics do not match complete fight pairs");
                    require(result.at("timeline").is_array() && result.at("timeline").size() == result.at("results")[0].at("decisions") &&
                        !result.contains("curves") && !result.contains("analyzedDecisions"), "Invalid policy trajectory representation");
                    require(result.at("timing").is_object(), "Missing policy search cost");
                    for (const char* key : {"setupMs", "evaluationWallMs", "totalWallMs", "searchDispatchWallMs", "searchRolloutFights", "replayFights", "dispatches"}) numeric(result.at("timing"), key);
                }
                summary["analysis"] = result;
                summary["timingScope"] = "Separate search/policy benchmark; no benchmark throughput is measured or accepted.";
                activeOutput.clear(); fs::remove_all(workDirectory); workDirectory.clear();
                saveJson(request.outputDirectory / "summary.json", summary);
                { std::lock_guard<std::mutex> lock(mutex_); state_.completedFights = request.totalFights; state_.summary = summary; }
                break;
            }
            Json timing = validateNativeResult(result, request, suite);
            if (request.rawReports) require(fs::file_size(activeRaw) == uint64_t(request.totalFights) * (request.mode == "detailed" ? 163 : 16) * 4, "Native raw report size mismatch");
            if (request.mode == "detailed") {
                require(result.at("details").at("path") == activeDetails.string(), "Detailed output path mismatch");
                require(fs::file_size(activeDetails) <= 32 * 1024 * 1024, "Detailed output exceeds bounded trace size");
                const auto analysis = readJson(activeDetails);
                require(analysis.at("schemaVersion") == 1 && analysis.at("mode") == "detailed" && analysis.at("suiteHash") == result.at("suiteHash") && analysis.at("candidates").size() == suite.at("scenarios").size(), "Invalid detailed analysis identity");
                for (size_t c = 0; c < analysis.at("candidates").size(); ++c) {
                    const auto& candidate = analysis.at("candidates")[c];
                    require(candidate.at("candidateId") == suite.at("scenarios")[c].at("id") && candidate.at("fights") == request.totalFights / suite.at("scenarios").size(), "Detailed candidate identity mismatch");
                    require(candidate.at("timeline").at("reportReplay") == "bit-identical", "Timeline replay failed");
                }
            }
            timing["clientWallMs"] = milliseconds(started);
            if (!previous.is_null()) {
                for (const char* key : {"results", "suiteHash", "inputSnapshotHash", "kernelHashes", "optimization"}) require(result.at(key) == previous.at(key), std::string("Repeated native result changed: ") + key);
                require(result.at("probe").at("device") == previous.at("probe").at("device"), "Repeated native device changed");
            }
            previous = result;
            summary["runs"].push_back({{"nativeResult", activeOutput.filename().string()}, {"timing", timing}, {"native", result}});
            activeOutput.clear(); activeRaw.clear(); activeDetails.clear(); fs::remove_all(workDirectory); workDirectory.clear();
            saveJson(request.outputDirectory / "summary.json", summary);
            { std::lock_guard<std::mutex> lock(mutex_); state_.completedFights = request.totalFights; state_.summary = summary; }
        }
        require(!cancelRequested_.load(), "Cancelled");
        if (request.mode != "regret" && request.mode != "policy_benchmark") {
            summary["medians"] = Json::object();
            for (const char* metric : {"gpuComputeMs", "nativeWallMs", "executeReadbackMs", "cDecodeMs", "gpuSimsPerSecond", "nativeSimsPerSecond", "steadySimsPerSecond", "clientWallMs"}) {
                std::vector<double> values; for (const auto& run : summary["runs"]) values.push_back(run["timing"][metric].get<double>());
                std::sort(values.begin(), values.end()); const size_t n = values.size();
                summary["medians"][metric] = (values[(n - 1) / 2] + values[n / 2]) / 2;
            }
        }
        summary["status"] = "complete";
    } catch (const std::exception& error) {
        if (child > 0) { kill(-child, SIGKILL); while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {} }
        if (readFd >= 0) close(readFd);
        std::error_code ignored;
        if (!activeOutput.empty()) fs::remove(activeOutput, ignored);
        if (!activeRaw.empty()) fs::remove(activeRaw, ignored);
        if (!activeDetails.empty()) { fs::remove(activeDetails, ignored); fs::remove(activeDetails.string() + ".tmp", ignored); }
        if (!workDirectory.empty()) fs::remove_all(workDirectory, ignored);
        summary["status"] = cancelRequested_.load() ? "cancelled" : "failed";
        summary["error"] = error.what(); summary.erase("medians");
    }
    try { saveJson(request.outputDirectory / "summary.json", summary); }
    catch (const std::exception& error) { summary["status"] = "failed"; summary["error"] = error.what(); summary.erase("medians"); }
    { std::lock_guard<std::mutex> lock(mutex_); state_.status = summary["status"]; state_.phase = state_.status; state_.error = summary.value("error", ""); state_.summary = std::move(summary); }
    running_.store(false);
}
}
