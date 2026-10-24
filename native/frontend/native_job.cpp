#include "native_job.h"
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
double milliseconds(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
}
Json validateNativeResult(const Json& result, const JobRequest& request, const Json& suite) {
    require(result.at("schemaVersion") == 1 && result.at("classId") == "warlock" && result.at("status") == "complete", "Incomplete or unsupported native result");
    const auto iterations = request.totalFights / suite.at("scenarios").size();
    require(result.at("iterations") == iterations && result.at("seed") == suite.at("seed") && result.at("suiteId") == suite.at("id"), "Native workload identity mismatch");
    require(result.at("mode") == "fast" && result.at("runtime").at("backend") == "Vulkan", "Wrong native mode/backend");
    require(result.at("kernelHashes") == suite.at("kernelHashes"), "Native source snapshot mismatch");
    for (const char* name : {"suiteHash", "inputSnapshotHash"})
        require(result.at(name).is_string() && result.at(name).get<std::string>().size() == 64, std::string("Missing identity: ") + name);
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
    unsigned completed = 0, total = 0;
    if (std::sscanf(line.c_str(), "Read back %u/%u fights", &completed, &total) == 2 && total == state_.totalFights && completed <= total) {
        state_.completedFights = completed; state_.phase = "Native GPU execution / C aggregation";
    } else if (line.find("Warmup complete:") != std::string::npos) state_.phase = "Measured native dispatches";
    else if (line.find("Pipeline ready") != std::string::npos) state_.phase = "Native pipeline ready / discarded warmups";
}
void NativeJob::start(const JobRequest& input) {
    require(!running(), "A native job is already running");
    wait();
    JobRequest request = input;
    request.root = fs::canonical(request.root);
    const Json suite = readJson(request.root / "data/validation/forever_baseline.json");
    const size_t candidates = suite.at("scenarios").size();
    require(candidates && request.totalFights % candidates == 0 && request.totalFights / candidates >= 2 && request.totalFights / candidates <= 1048576 && request.totalFights <= 16777216, "Invalid fight count for frozen suite");
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
        {"computeBackend", "unchanged tools/run_native.lua + tools/vulkan_probe.c"},
        {"presentation", request.presentation}, {"totalFights", request.totalFights}, {"batchSize", request.batchSize},
        {"warmupBatches", request.warmupBatches}, {"requestedRuns", request.runs}, {"rawReports", request.rawReports}, {"runs", Json::array()},
        {"timingScope", "GPU/native/steady metrics come from native engine. Client wall includes process startup and JSON loading. UI rendering mode is labeled separately; rendering performance is not inferred from headless results."},
        {"validation", "diagnostic-only; hardware GUI performance gates and accepted Forever parity remain separate"}};
    pid_t child = -1;
    int readFd = -1;
    fs::path activeOutput, activeRaw, workDirectory;
    try {
        const Json suite = readJson(request.root / "data/validation/forever_baseline.json");
        Json previous;
        for (uint32_t index = 0; index < request.runs; ++index) {
            require(!cancelRequested_.load(), "Cancelled");
            { std::lock_guard<std::mutex> lock(mutex_); state_.run = index + 1; state_.completedFights = 0; }
            setPhase("Preparing native worker / shader cache");
            activeOutput = request.outputDirectory / ("native-" + std::to_string(index + 1) + ".json");
            activeRaw = request.outputDirectory / ("native-" + std::to_string(index + 1) + ".bin");
            workDirectory = request.outputDirectory / ("work-" + std::to_string(index + 1));
            std::vector<std::string> command = runner_;
            for (const auto& value : std::vector<std::string>{"--total-fights", std::to_string(request.totalFights), "--batch-size", std::to_string(request.batchSize), "--warmup-batches", std::to_string(request.warmupBatches), "--output", activeOutput.string(), "--work-dir", workDirectory.string()}) command.push_back(value);
            if (!request.specialized) command.push_back("--generic");
            else if (!request.wave32) command.push_back("--no-subgroup-size");
            if (request.allowSoftware) command.push_back("--allow-software");
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
            Json timing = validateNativeResult(result, request, suite);
            if (request.rawReports) require(fs::file_size(activeRaw) == uint64_t(request.totalFights) * 16 * 4, "Native raw report size mismatch");
            timing["clientWallMs"] = milliseconds(started);
            if (!previous.is_null()) {
                for (const char* key : {"results", "suiteHash", "inputSnapshotHash", "kernelHashes", "optimization"}) require(result.at(key) == previous.at(key), std::string("Repeated native result changed: ") + key);
                require(result.at("probe").at("device") == previous.at("probe").at("device"), "Repeated native device changed");
            }
            previous = result;
            summary["runs"].push_back({{"nativeResult", activeOutput.filename().string()}, {"timing", timing}, {"native", result}});
            activeOutput.clear(); activeRaw.clear(); fs::remove_all(workDirectory); workDirectory.clear();
            saveJson(request.outputDirectory / "summary.json", summary);
            { std::lock_guard<std::mutex> lock(mutex_); state_.completedFights = request.totalFights; state_.summary = summary; }
        }
        require(!cancelRequested_.load(), "Cancelled");
        summary["medians"] = Json::object();
        for (const char* metric : {"gpuComputeMs", "nativeWallMs", "executeReadbackMs", "cDecodeMs", "gpuSimsPerSecond", "nativeSimsPerSecond", "steadySimsPerSecond", "clientWallMs"}) {
            std::vector<double> values; for (const auto& run : summary["runs"]) values.push_back(run["timing"][metric].get<double>());
            std::sort(values.begin(), values.end()); const size_t n = values.size();
            summary["medians"][metric] = (values[(n - 1) / 2] + values[n / 2]) / 2;
        }
        summary["status"] = "complete";
    } catch (const std::exception& error) {
        if (child > 0) { kill(-child, SIGKILL); while (waitpid(child, nullptr, 0) < 0 && errno == EINTR) {} }
        if (readFd >= 0) close(readFd);
        std::error_code ignored;
        if (!activeOutput.empty()) fs::remove(activeOutput, ignored);
        if (!activeRaw.empty()) fs::remove(activeRaw, ignored);
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
