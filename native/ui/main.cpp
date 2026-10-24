#include "native_job.h"
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_opengl3.h"
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace fastsim;
namespace fs = std::filesystem;
static volatile std::sig_atomic_t interrupted = 0;
static void interrupt(int signal) { interrupted = signal; }
struct Options {
    JobRequest request;
    bool headless = false;
    bool benchmark = false;
    bool computePriority = true;
    int uiFrames = 0;
    fs::path screenshot;
};
static uint32_t parseNumber(const std::string& text) {
    size_t end = 0; auto value = std::stoul(text, &end);
    if (end != text.size() || value > UINT32_MAX || text.empty() || text[0] == '-') throw std::runtime_error("Invalid integer: " + text);
    return static_cast<uint32_t>(value);
}
static Options arguments(int argc, char** argv) {
    Options options; options.request.root = FASTSIM_SOURCE_DIR;
    for (int i = 1; i < argc; ++i) {
        const std::string name = argv[i];
        auto value = [&]() { if (++i >= argc) throw std::runtime_error("Missing " + name); return std::string(argv[i]); };
        if (name == "--headless") options.headless = true;
        else if (name == "--benchmark") options.benchmark = true;
        else if (name == "--live-ui") options.computePriority = false;
        else if (name == "--allow-software") options.request.allowSoftware = true;
        else if (name == "--generic") { options.request.specialized = false; options.request.wave32 = false; }
        else if (name == "--no-subgroup-size") options.request.wave32 = false;
        else if (name == "--raw-reports") options.request.rawReports = true;
        else if (name == "--runs") options.request.runs = parseNumber(value());
        else if (name == "--total-fights") options.request.totalFights = parseNumber(value());
        else if (name == "--batch-size") options.request.batchSize = parseNumber(value());
        else if (name == "--warmup-batches") options.request.warmupBatches = parseNumber(value());
        else if (name == "--output-dir") options.request.outputDirectory = value();
        else if (name == "--root") options.request.root = value();
        else if (name == "--ui-frames") options.uiFrames = static_cast<int>(parseNumber(value()));
        else if (name == "--screenshot") options.screenshot = value();
        else if (name == "--help") {
            std::cout << "Fastsim / thin Dear ImGui frontend\n"
                "  --benchmark                 Run native job with GUI attached, then exit\n"
                "  --headless                  Same worker without SDL/window initialization\n"
                "  --live-ui                   Render at 10 Hz during jobs (default pauses rendering)\n"
                "  --runs N --total-fights N --batch-size N --warmup-batches N\n"
                "  --generic --no-subgroup-size --allow-software --raw-reports\n"
                "  --output-dir PATH           Fresh directory; retains native results and summary\n";
            std::exit(0);
        } else throw std::runtime_error("Unknown option: " + name);
    }
    options.request.presentation = options.headless ? "headless" : options.computePriority ? "rendering-paused" : "visible-10hz";
    return options;
}
static void printSummary(const JobSnapshot& snapshot) {
    if (snapshot.status == "complete") {
        const auto& median = snapshot.summary.at("medians");
        std::cout << "Native engine median (" << snapshot.summary.at("presentation").get<std::string>() << "): GPU "
            << static_cast<uint64_t>(median.at("gpuSimsPerSecond").get<double>()) << " sims/sec; native wall "
            << static_cast<uint64_t>(median.at("nativeSimsPerSecond").get<double>()) << " sims/sec; steady "
            << static_cast<uint64_t>(median.at("steadySimsPerSecond").get<double>()) << " sims/sec\n";
    } else std::cerr << snapshot.status << ": " << snapshot.error << '\n' << snapshot.log;
    std::cout << "Saved " << snapshot.outputDirectory << "/summary.json\n";
}
static void savePresentation(const JobSnapshot& snapshot, bool rendered, uint32_t frames) {
    if (snapshot.outputDirectory.empty()) return;
    const fs::path path = fs::path(snapshot.outputDirectory) / "summary.json";
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot read worker summary for presentation metadata");
    auto summary = nlohmann::json::parse(input);
    summary["uiObservations"] = {{"rendererInitialized", rendered}, {"renderer", rendered ? "SDL3 + OpenGL3 (UI only)" : "none"},
        {"imguiRevision", FASTSIM_IMGUI_REVISION}, {"frontendSourceHash", FASTSIM_FRONTEND_SOURCE_HASH}, {"frontendBuildType", FASTSIM_FRONTEND_BUILD_TYPE}, {"sdlVersion", SDL_GetVersion()}, {"framesDuringNativeJobs", frames},
        {"note", "Frame count records frontend rendering calls, not compositor or other applications. GPU performance still needs paired hardware measurements."}};
    const auto temporary = path.string() + ".tmp";
    { std::ofstream output(temporary); output << summary.dump(2) << '\n'; if (!output) throw std::runtime_error("Cannot save presentation metadata"); }
    fs::rename(temporary, path);
}
static int headless(Options options) {
    NativeJob job; job.start(options.request);
    uint32_t lastRun = 0, lastCount = 0;
    while (job.running()) {
        if (interrupted) job.cancel();
        auto state = job.snapshot();
        if (state.run != lastRun || state.completedFights != lastCount) {
            std::cout << "Native run " << state.run << "/" << options.request.runs << ": " << state.completedFights << "/" << state.totalFights << '\n';
            lastRun = state.run; lastCount = state.completedFights;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    job.wait(); const auto state = job.snapshot(); savePresentation(state, false, 0); printSummary(state); return state.status == "complete" ? 0 : 1;
}
static void screenshot(const fs::path& path, SDL_Window* window) {
    int width = 0, height = 0; SDL_GetWindowSizeInPixels(window, &width, &height);
    std::vector<unsigned char> pixels(static_cast<size_t>(width) * height * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1); glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    std::ofstream output(path, std::ios::binary); output << "P6\n" << width << ' ' << height << "\n255\n";
    for (int y = height - 1; y >= 0; --y) output.write(reinterpret_cast<const char*>(pixels.data() + static_cast<size_t>(y) * width * 3), width * 3);
    if (!output) throw std::runtime_error("Cannot save screenshot: " + path.string());
}
static int gui(Options options) {
    if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_Window* window = SDL_CreateWindow("Fastsim / native Vulkan + Dear ImGui", 980, 660, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) { std::string message = SDL_GetError(); SDL_Quit(); throw std::runtime_error(message); }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) { std::string message = SDL_GetError(); SDL_DestroyWindow(window); SDL_Quit(); throw std::runtime_error(message); }
    SDL_GL_MakeCurrent(window, context); SDL_GL_SetSwapInterval(0);
    IMGUI_CHECKVERSION(); ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    if (!ImGui_ImplSDL3_InitForOpenGL(window, context) || !ImGui_ImplOpenGL3_Init("#version 330 core")) throw std::runtime_error("Cannot initialize Dear ImGui backends");
    NativeJob job;
    bool done = false, pending = options.benchmark, reported = false;
    std::string error;
    uint32_t framesDuringJobs = 0;
    int frames = 0;
    while (!done) {
        if (interrupted) { job.cancel(); done = true; break; }
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT || (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window))) { job.cancel(); done = true; }
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) { if (job.running()) job.cancel(); else done = true; }
        }
        if (done) break;
        auto state = job.snapshot();
        if (!job.running() && state.status != "idle" && !reported) {
            job.wait(); state = job.snapshot(); savePresentation(state, true, framesDuringJobs); printSummary(state); reported = true;
            std::cout << "UI frames during native job: " << framesDuringJobs << '\n';
            if (options.benchmark) { done = true; continue; }
        }
        const bool paused = job.running() && options.computePriority;
        if (paused) {
            const std::string title = "Fastsim / compute priority / run " + std::to_string(state.run) + " / " + std::to_string(state.completedFights) + " fights / Escape cancels";
            SDL_SetWindowTitle(window, title.c_str());
            SDL_Delay(50); continue; // No renderer frame, swap, or GPU work while native job runs.
        }
        SDL_SetWindowTitle(window, "Fastsim / native Vulkan + Dear ImGui");
        const bool drawingDuringJob = job.running();
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplSDL3_NewFrame(); ImGui::NewFrame();
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos); ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::Begin("Native compute", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextUnformatted("FASTSIM / native Vulkan compute");
        ImGui::TextDisabled("Dear ImGui is a job client. The existing native engine owns all simulation work.");
        ImGui::Separator();
        ImGui::BeginDisabled(job.running());
        int total = static_cast<int>(options.request.totalFights), runs = static_cast<int>(options.request.runs);
        if (ImGui::InputInt("Fights per run", &total, 5000, 100000)) options.request.totalFights = static_cast<uint32_t>(std::max(total, 10));
        ImGui::TextDisabled("Five fixed candidates; fight count must be a multiple of five (minimum ten).");
        if (ImGui::InputInt("Runs", &runs)) options.request.runs = static_cast<uint32_t>(std::clamp(runs, 1, 10));
        if (ImGui::Checkbox("Shared-feature specialization", &options.request.specialized) && !options.request.specialized) options.request.wave32 = false;
        ImGui::BeginDisabled(!options.request.specialized); ImGui::Checkbox("Require wave32 (unsupported hardware rejects)", &options.request.wave32); ImGui::EndDisabled();
        ImGui::Checkbox("Compute priority: pause rendering during native jobs", &options.computePriority);
        ImGui::TextDisabled("Heap40 | batch %u | warmup batches %u | 180-second frozen suite", options.request.batchSize, options.request.warmupBatches);
        if (ImGui::Button("Run native benchmark")) pending = true;
        ImGui::EndDisabled();
        ImGui::SameLine(); ImGui::BeginDisabled(!job.running()); if (ImGui::Button("Cancel / Escape")) job.cancel(); ImGui::EndDisabled();
        if (pending && !job.running()) ImGui::TextUnformatted(options.computePriority ? "Starting: rendering will pause. Escape or close the window to cancel." : "Starting: live UI renders at 10 Hz; shared-GPU contention will be measured separately.");
        const float progress = state.totalFights ? std::min(1.0f, (static_cast<float>(state.run ? state.run - 1 : 0) + static_cast<float>(state.completedFights) / state.totalFights) / options.request.runs) : 0;
        ImGui::ProgressBar(state.status == "complete" ? 1.0f : progress);
        ImGui::Text("Status: %s | %s", state.status.c_str(), state.phase.c_str());
        if (!error.empty()) ImGui::TextWrapped("%s", error.c_str());
        if (!state.error.empty()) ImGui::TextWrapped("%s", state.error.c_str());
        if (state.summary.contains("medians") && state.status == "complete") {
            const auto& medians = state.summary.at("medians");
            ImGui::Separator(); ImGui::Text("GPU: %.0f fights/sec", medians.at("gpuSimsPerSecond").get<double>());
            ImGui::Text("Native full run: %.0f fights/sec | steady: %.0f fights/sec", medians.at("nativeSimsPerSecond").get<double>(), medians.at("steadySimsPerSecond").get<double>());
            ImGui::Text("C aggregation: %.3f ms | native process/client: %.3f ms", medians.at("cDecodeMs").get<double>(), medians.at("clientWallMs").get<double>());
            ImGui::Text("Mode: %s", state.summary.at("presentation").get<std::string>().c_str());
            const auto& result = state.summary.at("runs").back().at("native");
            ImGui::Text("Device: %s", result.at("probe").at("device").get<std::string>().c_str());
            for (const auto& row : result.at("results")) ImGui::Text("%s: %.3f DPS (%u fights)", row.at("candidateId").get<std::string>().c_str(), row.at("meanDps").get<double>(), row.at("sampleCount").get<uint32_t>());
        }
        if (!state.outputDirectory.empty()) ImGui::TextWrapped("Results: %s/summary.json", state.outputDirectory.c_str());
        ImGui::Separator(); ImGui::TextWrapped("Paused-UI fixed workload: ~1.56M GPU fights/sec on RX 5600 XT, validated against headless. Live rendering, diverse workloads and accepted reference accuracy still require validation.");
        if (ImGui::CollapsingHeader("Native worker log")) ImGui::TextUnformatted(state.log.c_str());
        ImGui::End(); ImGui::Render();
        int width = 0, height = 0; SDL_GetWindowSizeInPixels(window, &width, &height);
        glViewport(0, 0, width, height); glClearColor(0.055f, 0.065f, 0.085f, 1); glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (!options.screenshot.empty() && !job.running()) { screenshot(options.screenshot, window); options.screenshot.clear(); }
        SDL_GL_SwapWindow(window); ++frames;
        if (drawingDuringJob) ++framesDuringJobs;
        if (pending && !job.running()) {
            // Finish the one announcement frame BEFORE launching the Vulkan worker.
            glFinish();
            options.request.presentation = options.computePriority ? "rendering-paused" : "visible-10hz";
            try { job.start(options.request); reported = false; framesDuringJobs = 0; error.clear(); }
            catch (const std::exception& exception) { error = exception.what(); std::cerr << error << '\n'; if (options.benchmark) { done = true; } }
            pending = false;
        }
        if (options.uiFrames > 0 && frames >= options.uiFrames) done = true;
        SDL_Delay(job.running() ? 100 : 33);
    }
    job.cancel(); job.wait();
    const auto final = job.snapshot();
    if (final.status != "idle" && !reported) savePresentation(final, true, framesDuringJobs);
    const int status = options.benchmark ? (final.status == "complete" ? 0 : 1) : 0;
    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplSDL3_Shutdown(); ImGui::DestroyContext();
    SDL_GL_DestroyContext(context); SDL_DestroyWindow(window); SDL_Quit();
    return status;
}
int main(int argc, char** argv) {
    std::signal(SIGINT, interrupt); std::signal(SIGTERM, interrupt);
    try { Options options = arguments(argc, argv); const int status = options.headless ? headless(options) : gui(options); return interrupted ? 128 + interrupted : status; }
    catch (const std::exception& error) { std::cerr << "Fastsim: " << error.what() << '\n'; return 1; }
}
