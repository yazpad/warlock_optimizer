#include "native_job.h"
#include "dps_histogram.h"
#include "editor.h"
#include "detailed_view.h"
#include <memory>
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
    fs::path candidate;
};
static uint32_t parseNumber(const std::string& text) {
    size_t end = 0; auto value = std::stoul(text, &end);
    if (end != text.size() || value > UINT32_MAX || text.empty() || text[0] == '-') throw std::runtime_error("Invalid integer: " + text);
    return static_cast<uint32_t>(value);
}
static Options arguments(int argc, char** argv) {
    Options options; options.request.histogram = true; options.request.root = FASTSIM_SOURCE_DIR;
    for (int i = 1; i < argc; ++i) {
        const std::string name = argv[i];
        auto value = [&]() { if (++i >= argc) throw std::runtime_error("Missing " + name); return std::string(argv[i]); };
        if (name == "--detailed") options.request.mode = "detailed";
        else if (name == "--no-histogram") options.request.histogram = false;
        else if (name == "--candidate") options.candidate = value();
        else if (name == "--headless") options.headless = true;
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
                "  --detailed                  Detailed counters and first-fight timeline\n"
                "  --candidate PATH            Prepare a saved editor build (GUI or headless)\n"
                "  --benchmark                 Run native job with GUI attached, then exit\n"
                "  --headless                  Same worker without SDL/window initialization\n"
                "  --live-ui                   Render at 10 Hz during jobs (default pauses rendering)\n"
                "  --runs N --total-fights N --batch-size N --warmup-batches N\n"
                "  --generic --no-subgroup-size --allow-software --raw-reports --no-histogram\n"
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
static void drawHistogram(const DpsHistogram& chart) {
    ImGui::TextDisabled("%u fights / mean %.1f DPS", chart.sampleCount, chart.mean);
    const float peak = *std::max_element(chart.bins.begin(), chart.bins.end());
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(0.40f, 0.65f, 0.92f, 1));
    ImGui::PlotHistogram("##dps-distribution", chart.bins.data(), static_cast<int>(chart.bins.size()), 0, nullptr, 0, peak * 1.15f, ImVec2(-1, 150));
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) {
        const float left = ImGui::GetItemRectMin().x + ImGui::GetStyle().FramePadding.x;
        const float width = ImGui::GetItemRectSize().x - 2 * ImGui::GetStyle().FramePadding.x;
        const float position = std::clamp((ImGui::GetMousePos().x - left) / std::max(1.0f, width), 0.0f, 0.99999f);
        const size_t bin = static_cast<size_t>(position * chart.bins.size());
        const double step = (chart.high - chart.low) / chart.bins.size();
        ImGui::SetTooltip("%.1f - %.1f DPS\n%.0f fights", chart.low + bin * step, chart.low + (bin + 1) * step, chart.bins[bin]);
    }
    ImGui::TextDisabled("%.1f DPS", chart.low); ImGui::SameLine();
    char high[64]; std::snprintf(high, sizeof(high), "%.1f DPS", chart.high);
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(high).x));
    ImGui::TextDisabled("%s", high);
}
static int gui(Options options) {
    if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_Window* window = SDL_CreateWindow("Fastsim / native Vulkan + Dear ImGui", 1200, 900, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (!window) { std::string message = SDL_GetError(); SDL_Quit(); throw std::runtime_error(message); }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) { std::string message = SDL_GetError(); SDL_DestroyWindow(window); SDL_Quit(); throw std::runtime_error(message); }
    SDL_GL_MakeCurrent(window, context); SDL_GL_SetSwapInterval(0);
    IMGUI_CHECKVERSION(); ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle(); style.WindowPadding = ImVec2(24, 20); style.FramePadding = ImVec2(9, 6); style.ItemSpacing = ImVec2(10, 8);
    style.FrameRounding = 5; style.ChildRounding = 6; style.GrabRounding = 4;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.065f, 0.085f, 1);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.12f, 0.16f, 1);
    style.Colors[ImGuiCol_Button] = ImVec4(0.20f, 0.27f, 0.40f, 1);
    const char* font = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
    if (fs::exists(font)) ImGui::GetIO().Fonts->AddFontFromFileTTF(font, 16);
    if (!ImGui_ImplSDL3_InitForOpenGL(window, context) || !ImGui_ImplOpenGL3_Init("#version 330 core")) throw std::runtime_error("Cannot initialize Dear ImGui backends");
    NativeJob job;
    std::vector<DpsHistogram> histograms;
    DetailedView detailedView;
    std::string histogramError;
    int histogramCandidate = 0;
    auto editor = std::make_unique<Editor>(options.request.root, options.request.candidateSource);
    bool frozenBenchmark = options.benchmark || options.headless;
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
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) { if (job.running()) { job.cancel(); } else done = true; }
        }
        if (done) break;
        auto state = job.snapshot();
        if (!job.running() && state.status != "idle" && !reported) {
            job.wait(); state = job.snapshot(); savePresentation(state, true, framesDuringJobs); printSummary(state); reported = true;
            std::cout << "UI frames during native job: " << framesDuringJobs << '\n';
            if (state.status == "complete" && options.request.histogram) {
                try {
                    for (const auto& row : state.summary.at("runs").back().at("native").at("results")) histograms.push_back(histogramFromResult(row));
                } catch (const std::exception& e) { histograms.clear(); histogramError = e.what(); }
            }
            if (state.status == "complete" && state.summary.at("mode") == "detailed") {
                try { detailedView.load(state.summary.at("runs").back().at("native").at("details").at("path").get<std::string>()); }
                catch (const std::exception& e) { error = e.what(); }
            }
            if (options.benchmark) { done = true; continue; }
        }
        const bool busy = job.running();
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
        ImGui::TextUnformatted("FASTSIM"); ImGui::SameLine(); ImGui::TextDisabled("WARLOCK");
        ImGui::Separator();
        editor->draw(busy);
        ImGui::SeparatorText("Simulation");
        ImGui::BeginDisabled(busy);
        int total = static_cast<int>(options.request.totalFights), runs = static_cast<int>(options.request.runs);
        ImGui::PushItemWidth(170);
        if (ImGui::InputInt("Fights", &total, 5000, 100000)) options.request.totalFights = static_cast<uint32_t>(std::clamp(total, 10, 1048576));
        ImGui::SameLine();
        if (ImGui::InputInt("Runs", &runs)) options.request.runs = static_cast<uint32_t>(std::clamp(runs, 1, 10));
        ImGui::PopItemWidth();
        if (ImGui::CollapsingHeader("Advanced")) {
            ImGui::Checkbox("Frozen five-build benchmark", &frozenBenchmark);
            ImGui::Checkbox("DPS histogram", &options.request.histogram);
            if (ImGui::Checkbox("Specialization", &options.request.specialized) && !options.request.specialized) options.request.wave32 = false;
            ImGui::BeginDisabled(!options.request.specialized); ImGui::Checkbox("Wave32", &options.request.wave32); ImGui::EndDisabled();
            ImGui::Checkbox("Pause UI during compute", &options.computePriority);
        }
        const bool fastClicked = ImGui::Button("Simulate", ImVec2(150, 0));
        ImGui::SameLine(); const bool detailedClicked = ImGui::Button("Run detailed");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Configured fights, detailed counters, and timeline of fight #1 per build. Slower; fast mode stays unchanged.");
        if (fastClicked || detailedClicked) {
            options.request.mode = detailedClicked ? "detailed" : "fast";
            try {
                if (frozenBenchmark) { options.request.suiteSource.clear(); options.request.suiteHash.clear(); options.request.candidateSource.clear(); }
                else editor->submit(options.request);
                pending = true; error.clear();
            } catch (const std::exception& e) { error = e.what(); }
        }
        ImGui::EndDisabled();
        ImGui::SameLine(); ImGui::BeginDisabled(!busy); if (ImGui::Button("Cancel")) { job.cancel(); } ImGui::EndDisabled();
        if (pending && !job.running()) ImGui::TextDisabled("Running / Escape to cancel");
        const float progress = state.totalFights ? std::min(1.0f, (static_cast<float>(state.run ? state.run - 1 : 0) + static_cast<float>(state.completedFights) / state.totalFights) / options.request.runs) : 0;
        ImGui::ProgressBar(state.status == "complete" ? 1.0f : progress);
        ImGui::TextDisabled("%s", state.status.c_str());
        if (!error.empty()) ImGui::TextWrapped("%s", error.c_str());
        if (!state.error.empty()) ImGui::TextWrapped("%s", state.error.c_str());
        if (state.summary.contains("medians") && state.status == "complete") {
            const auto& medians = state.summary.at("medians");
            ImGui::SeparatorText("Results"); ImGui::Text("GPU: %.0f fights/sec", medians.at("gpuSimsPerSecond").get<double>());
            ImGui::Text("Native full run: %.0f fights/sec | steady: %.0f fights/sec", medians.at("nativeSimsPerSecond").get<double>(), medians.at("steadySimsPerSecond").get<double>());
            ImGui::Text("C aggregation: %.3f ms | native process/client: %.3f ms", medians.at("cDecodeMs").get<double>(), medians.at("clientWallMs").get<double>());
            ImGui::Text("Mode: %s / %s", state.summary.at("mode").get<std::string>().c_str(), state.summary.at("presentation").get<std::string>().c_str());
            const auto& result = state.summary.at("runs").back().at("native");
            ImGui::Text("Device: %s", result.at("probe").at("device").get<std::string>().c_str());
            for (const auto& row : result.at("results")) ImGui::Text("%s: %.3f DPS (%u fights)", row.at("candidateId").get<std::string>().c_str(), row.at("meanDps").get<double>(), row.at("sampleCount").get<uint32_t>());
        }
        if (!state.outputDirectory.empty()) ImGui::TextWrapped("Results: %s/summary.json", state.outputDirectory.c_str());
        ImGui::SeparatorText("DPS distribution");
        if (histograms.empty() && histogramError.empty()) ImGui::TextDisabled(state.status == "complete" ? "Histogram disabled for this run" : "Available after simulation");
        if (!histogramError.empty()) ImGui::TextWrapped("%s", histogramError.c_str());
        if (!histograms.empty()) {
            if (histograms.size() > 1 && ImGui::BeginCombo("Build##histogram", histograms[histogramCandidate].candidateId.c_str())) {
                for (size_t i = 0; i < histograms.size(); ++i) if (ImGui::Selectable(histograms[i].candidateId.c_str(), histogramCandidate == static_cast<int>(i))) histogramCandidate = static_cast<int>(i);
                ImGui::EndCombo();
            }
            drawHistogram(histograms[histogramCandidate]);
        }
        detailedView.draw();
        if (ImGui::CollapsingHeader("Diagnostics")) {
            ImGui::TextDisabled("%s", state.phase.c_str());
            ImGui::TextUnformatted(state.log.c_str());
        }
        ImGui::End(); ImGui::Render();
        int width = 0, height = 0; SDL_GetWindowSizeInPixels(window, &width, &height);
        glViewport(0, 0, width, height); glClearColor(0.055f, 0.065f, 0.085f, 1); glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (!options.screenshot.empty() && !busy && (options.uiFrames <= 0 || frames + 1 >= options.uiFrames)) { screenshot(options.screenshot, window); options.screenshot.clear(); }
        SDL_GL_SwapWindow(window); ++frames;
        if (drawingDuringJob) ++framesDuringJobs;
        if (pending && !busy) {
            // Finish the one announcement frame BEFORE launching the Vulkan worker.
            glFinish();
            options.request.presentation = options.computePriority ? "rendering-paused" : "visible-10hz";
            try {
                detailedView.clear(); histograms.clear(); histogramCandidate = 0; histogramError.clear();
                job.start(options.request); reported = false; framesDuringJobs = 0; error.clear();
            }
            catch (const std::exception& exception) { error = exception.what(); std::cerr << error << '\n'; if (options.benchmark) { done = true; } }
            pending = false;
        }
        if (options.uiFrames > 0 && frames >= options.uiFrames) done = true;
        SDL_Delay(busy ? 100 : 33);
    }
    job.cancel(); job.wait();
    const auto final = job.snapshot();
    if (final.status != "idle" && !reported) savePresentation(final, true, framesDuringJobs);
    const int status = options.benchmark ? (final.status == "complete" ? 0 : 1) : 0;
    editor.reset();
    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplSDL3_Shutdown(); ImGui::DestroyContext();
    SDL_GL_DestroyContext(context); SDL_DestroyWindow(window); SDL_Quit();
    return status;
}
int main(int argc, char** argv) {
    std::signal(SIGINT, interrupt); std::signal(SIGTERM, interrupt);
    try { Options options = arguments(argc, argv);
        if (!options.candidate.empty()) { std::ifstream file(options.candidate); if (!file) throw std::runtime_error("Cannot read candidate"); prepareCandidate(options.request, nlohmann::json::parse(file)); }
        const int status = options.headless ? headless(options) : gui(options); return interrupted ? 128 + interrupted : status; }
    catch (const std::exception& error) { std::cerr << "Fastsim: " << error.what() << '\n'; return 1; }
}
