#include "native_job.h"
#include "dps_histogram.h"
#include "editor.h"
#include "detailed_view.h"
#include <memory>
#include "imgui.h"
#include "implot.h"
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
    fs::path regretResult;
};
static uint32_t parseNumber(const std::string& text) {
    size_t end = 0; auto value = std::stoul(text, &end);
    if (end != text.size() || value > UINT32_MAX || text.empty() || text[0] == '-') throw std::runtime_error("Invalid integer: " + text);
    return static_cast<uint32_t>(value);
}
static Options arguments(int argc, char** argv) {
    Options options; options.request.histogram = true; options.request.root = FASTSIM_SOURCE_DIR;
    bool explicitPolicyFights = false;
    for (int i = 1; i < argc; ++i) {
        const std::string name = argv[i];
        auto value = [&]() { if (++i >= argc) throw std::runtime_error("Missing " + name); return std::string(argv[i]); };
        if (name == "--regret") { options.request.mode = "regret"; options.request.runs = 1; options.request.histogram = false; }
        else if (name == "--policy-benchmark") { options.request.mode = "policy_benchmark"; options.request.runs = 1; options.request.histogram = false; }
        else if (name == "--policy-algorithm") { const auto algo = value();
            if (algo != "mcts" && algo != "neuroevolution") throw std::runtime_error("Invalid policy algorithm");
            options.request.benchmarkAlgo = algo == "neuroevolution" ? 1 : 0; }
        else if (name == "--evolution-generations") options.request.evolutionGenerations = parseNumber(value());
        else if (name == "--evolution-population") options.request.evolutionPopulation = parseNumber(value());
        else if (name == "--evolution-seed") options.request.evolutionSeed = parseNumber(value());
        else if (name == "--policy-fights") { options.request.benchmarkTrials = parseNumber(value()); explicitPolicyFights = true; }
        else if (name == "--evolution-sigma") { const std::string text = value(); size_t end = 0;
            options.request.evolutionSigma = std::stof(text, &end);
            if (end != text.size()) throw std::runtime_error("Invalid mutation sigma"); }
        else if (name == "--policy-seed-start") options.request.benchmarkSeedStart = parseNumber(value());
        else if (name == "--policy-puct") { const std::string text = value(); size_t end = 0;
            options.request.benchmarkLearningRate = std::stof(text, &end);
            if (end != text.size()) throw std::runtime_error("Invalid PUCT weight"); }
        else if (name == "--regret-decisions") options.request.regretDecisions = parseNumber(value());
        else if (name == "--regret-rounds") options.request.regretRounds = parseNumber(value());
        else if (name == "--regret-samples") options.request.regretSamples = parseNumber(value());
        else if (name == "--regret-validation") options.request.regretValidation = parseNumber(value());
        else if (name == "--regret-depth") options.request.regretDepth = parseNumber(value());
        else if (name == "--regret-candidate") options.request.regretCandidate = parseNumber(value());
        else if (name == "--detailed") options.request.mode = "detailed";
        else if (name == "--no-histogram") options.request.histogram = false;
        else if (name == "--regret-result") options.regretResult = value();
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
                "  --policy-benchmark          Complete-fight MCTS controller evaluation\n"
                "  --policy-fights N --policy-puct X --policy-seed-start N\n"
                "  --policy-algorithm mcts|neuroevolution --evolution-generations N --evolution-population N --evolution-seed N --evolution-sigma X\n"
                "  --regret                    GPU regret analysis (one separate diagnostic job)\n"
                "  --regret-result PATH        Inspect a saved regret summary without simulation\n"
                "  --regret-decisions N --regret-rounds N --regret-samples N --regret-validation N --regret-depth N\n"
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
    if (options.request.benchmarkAlgo == 1 && !explicitPolicyFights) options.request.benchmarkTrials = 2048;
    options.request.presentation = options.headless ? "headless" : options.computePriority ? "rendering-paused" : "visible-10hz";
    return options;
}
static void printSummary(const JobSnapshot& snapshot) {
    if (snapshot.status == "complete") {
        if (snapshot.summary.value("mode", "") == "regret") {
            const auto& analysis = snapshot.summary.at("analysis");
            std::cout << "Regret analysis: " << analysis.at("analyzedDecisions") << " decisions; exact baseline replay passed\n";
            std::cout << "Saved " << snapshot.outputDirectory << "/summary.json\n";
            return;
        }
        if (snapshot.summary.value("mode", "") == "policy_benchmark") {
            const auto& analysis = snapshot.summary.at("analysis");
            std::cout << "Policy benchmark: " << analysis.value("completedFightPairs", 0) << " complete fight pairs; policy DPS: "
                      << analysis.value("meanAlgoDps", 0.0) << " vs Baseline: " << analysis.value("meanBaselineDps", 0.0) << "\n";
            std::cout << "Saved " << snapshot.outputDirectory << "/summary.json\n";
            return;
        }
        if (snapshot.summary.contains("medians")) {
            const auto& median = snapshot.summary.at("medians");
            std::cout << "Native engine median (" << snapshot.summary.at("presentation").get<std::string>() << "): GPU "
                << static_cast<uint64_t>(median.at("gpuSimsPerSecond").get<double>()) << " sims/sec; native wall "
                << static_cast<uint64_t>(median.at("nativeSimsPerSecond").get<double>()) << " sims/sec; steady "
                << static_cast<uint64_t>(median.at("steadySimsPerSecond").get<double>()) << " sims/sec\n";
        }
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
    const size_t n = chart.bins.size();
    if (n == 0) return;
    const double step = (chart.high - chart.low) / static_cast<double>(n);
    std::vector<double> xs(n), ys(n);
    for (size_t i = 0; i < n; ++i) {
        xs[i] = chart.low + (static_cast<double>(i) + 0.5) * step;
        ys[i] = static_cast<double>(chart.bins[i]);
    }
    const double peak = *std::max_element(chart.bins.begin(), chart.bins.end());
    ImPlotSpec spec;
    spec.FillColor = ImVec4(0.35f, 0.60f, 0.90f, 0.85f);
    spec.LineColor = ImVec4(0.55f, 0.78f, 1.0f, 1.0f);
    spec.LineWeight = 1.0f;
    ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(10, 8));
    if (ImPlot::BeginPlot("##dps_histogram_plot", ImVec2(-1, 160), ImPlotFlags_NoTitle | ImPlotFlags_NoLegend | ImPlotFlags_NoBoxSelect)) {
        ImPlot::SetupAxes("DPS", "Count", ImPlotAxisFlags_None, ImPlotAxisFlags_None);
        ImPlot::SetupAxisLimits(ImAxis_X1, chart.low, chart.high, ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, 0, peak * 1.15, ImPlotCond_Always);
        ImPlot::PlotBars("DPS Distribution", xs.data(), ys.data(), static_cast<int>(n), step * 0.95, spec);
        if (ImPlot::IsPlotHovered()) {
            const ImPlotPoint mouse = ImPlot::GetPlotMousePos();
            if (mouse.x >= chart.low && mouse.x < chart.high) {
                const size_t bin = std::clamp(static_cast<size_t>((mouse.x - chart.low) / step), size_t{0}, n - 1);
                ImGui::SetTooltip("%.1f - %.1f DPS\n%.0f fights", chart.low + bin * step, chart.low + (bin + 1) * step, chart.bins[bin]);
            }
        }
        ImPlot::EndPlot();
    }
    ImPlot::PopStyleVar();
}
static int gui(Options options) {
    if (!SDL_Init(SDL_INIT_VIDEO)) throw std::runtime_error(SDL_GetError());
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_Window* window = SDL_CreateWindow("WoW Forever Optimizer", 1600, 960, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (window) { SDL_SetWindowMinimumSize(window, 1280, 720); }
    if (!window) { std::string message = SDL_GetError(); SDL_Quit(); throw std::runtime_error(message); }
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context) { std::string message = SDL_GetError(); SDL_DestroyWindow(window); SDL_Quit(); throw std::runtime_error(message); }
    SDL_GL_MakeCurrent(window, context); SDL_GL_SetSwapInterval(0);
    IMGUI_CHECKVERSION(); ImGui::CreateContext(); ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();

    auto& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(20, 18);
    style.FramePadding = ImVec2(10, 6);
    style.ItemSpacing = ImVec2(10, 8);
    style.ItemInnerSpacing = ImVec2(8, 6);
    style.FrameRounding = 6.0f;
    style.ChildRounding = 6.0f;
    style.PopupRounding = 6.0f;
    style.GrabRounding = 4.0f;
    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;

    // Polished dark theme with deep slate & subtle warlock purple/ember accents
    style.Colors[ImGuiCol_WindowBg]         = ImVec4(0.06f, 0.07f, 0.09f, 1.00f);
    style.Colors[ImGuiCol_ChildBg]          = ImVec4(0.08f, 0.09f, 0.12f, 0.60f);
    style.Colors[ImGuiCol_PopupBg]          = ImVec4(0.09f, 0.10f, 0.13f, 0.98f);
    style.Colors[ImGuiCol_Border]           = ImVec4(0.20f, 0.22f, 0.28f, 0.40f);
    style.Colors[ImGuiCol_BorderShadow]     = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    style.Colors[ImGuiCol_FrameBg]          = ImVec4(0.12f, 0.14f, 0.18f, 0.70f);
    style.Colors[ImGuiCol_FrameBgHovered]   = ImVec4(0.17f, 0.20f, 0.26f, 0.85f);
    style.Colors[ImGuiCol_FrameBgActive]    = ImVec4(0.22f, 0.25f, 0.32f, 1.00f);
    style.Colors[ImGuiCol_TitleBg]          = ImVec4(0.08f, 0.09f, 0.12f, 1.00f);
    style.Colors[ImGuiCol_TitleBgActive]    = ImVec4(0.11f, 0.13f, 0.17f, 1.00f);
    style.Colors[ImGuiCol_MenuBarBg]        = ImVec4(0.08f, 0.09f, 0.12f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarBg]      = ImVec4(0.06f, 0.07f, 0.09f, 0.50f);
    style.Colors[ImGuiCol_ScrollbarGrab]    = ImVec4(0.22f, 0.25f, 0.32f, 0.70f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.30f, 0.34f, 0.42f, 0.90f);
    style.Colors[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.38f, 0.42f, 0.52f, 1.00f);
    style.Colors[ImGuiCol_CheckMark]        = ImVec4(0.68f, 0.48f, 0.95f, 1.00f);
    style.Colors[ImGuiCol_SliderGrab]       = ImVec4(0.60f, 0.42f, 0.88f, 0.90f);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.72f, 0.52f, 0.98f, 1.00f);
    style.Colors[ImGuiCol_Button]           = ImVec4(0.20f, 0.24f, 0.32f, 0.75f);
    style.Colors[ImGuiCol_ButtonHovered]    = ImVec4(0.27f, 0.32f, 0.42f, 0.90f);
    style.Colors[ImGuiCol_ButtonActive]     = ImVec4(0.34f, 0.39f, 0.50f, 1.00f);
    style.Colors[ImGuiCol_Header]           = ImVec4(0.22f, 0.18f, 0.32f, 0.70f);
    style.Colors[ImGuiCol_HeaderHovered]    = ImVec4(0.30f, 0.24f, 0.44f, 0.85f);
    style.Colors[ImGuiCol_HeaderActive]     = ImVec4(0.38f, 0.30f, 0.54f, 1.00f);
    style.Colors[ImGuiCol_Separator]        = ImVec4(0.20f, 0.22f, 0.28f, 0.45f);
    style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.35f, 0.38f, 0.48f, 0.70f);
    style.Colors[ImGuiCol_SeparatorActive]  = ImVec4(0.50f, 0.54f, 0.68f, 1.00f);
    style.Colors[ImGuiCol_ResizeGrip]       = ImVec4(0.25f, 0.28f, 0.36f, 0.35f);
    style.Colors[ImGuiCol_ResizeGripHovered]= ImVec4(0.35f, 0.39f, 0.50f, 0.65f);
    style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.45f, 0.50f, 0.65f, 0.90f);
    style.Colors[ImGuiCol_Tab]              = ImVec4(0.12f, 0.14f, 0.18f, 0.80f);
    style.Colors[ImGuiCol_TabHovered]       = ImVec4(0.26f, 0.22f, 0.38f, 0.90f);
    style.Colors[ImGuiCol_TabSelected]      = ImVec4(0.24f, 0.18f, 0.38f, 1.00f);
    style.Colors[ImGuiCol_TabDimmed]        = ImVec4(0.09f, 0.10f, 0.14f, 0.70f);
    style.Colors[ImGuiCol_TabDimmedSelected]= ImVec4(0.16f, 0.14f, 0.24f, 0.85f);
    style.Colors[ImGuiCol_TextSelectedBg]   = ImVec4(0.45f, 0.32f, 0.75f, 0.45f);

    const char* robotoPath = "/home/aaron-ma/.fonts/Roboto-Regular.ttf";
    const char* dejavuPath = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
    const char* monoPath = "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf";
    static const ImWchar glyphRanges[] = {
        0x0020, 0x00FF, // Basic Latin + Latin Supplement
        0x0370, 0x03FF, // Greek and Coptic (includes Δ U+0394)
        0x2000, 0x206F, // General Punctuation
        0,
    };
    if (fs::exists(robotoPath)) io.Fonts->AddFontFromFileTTF(robotoPath, 16.0f, nullptr, glyphRanges);
    else if (fs::exists(dejavuPath)) io.Fonts->AddFontFromFileTTF(dejavuPath, 16.0f, nullptr, glyphRanges);
    if (fs::exists(monoPath)) io.Fonts->AddFontFromFileTTF(monoPath, 15.0f);
    if (!ImGui_ImplSDL3_InitForOpenGL(window, context) || !ImGui_ImplOpenGL3_Init("#version 330 core")) throw std::runtime_error("Cannot initialize Dear ImGui backends");
    NativeJob job;
    std::vector<DpsHistogram> histograms;
    DetailedView detailedView;
    int regretFocusFrames = 0;
    int policyFocusFrames = 0;
    nlohmann::json regretAnalysis;
    nlohmann::json policyAnalysis;
    if (!options.regretResult.empty()) {
        std::ifstream input(options.regretResult);
        if (!input) throw std::runtime_error("Cannot open saved regret analysis");
        auto saved = nlohmann::json::parse(input);
        regretFocusFrames = 2;
        regretAnalysis = saved.contains("analysis") ? saved.at("analysis") : saved;
        if (regretAnalysis.at("mode") != "regret" || !regretAnalysis.at("rows").is_array()) throw std::runtime_error("Invalid saved regret analysis");
    }
    std::string histogramError;
    int histogramCandidate = 0;
    auto editor = std::make_unique<Editor>(options.request.root, options.request.candidateSource);
    bool frozenBenchmark = options.benchmark || options.headless;
    JobRequest submittedRequest = options.request;
    bool done = false, pending = options.benchmark || options.request.mode == "regret", reported = false;
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
            if (event.type == SDL_EVENT_DROP_FILE) {
                if (editor) editor->handleDropFile(event.drop.data);
            } else if (event.type == SDL_EVENT_DROP_TEXT) {
                if (editor) editor->handleDropText(event.drop.data);
            }
        }
        if (done) break;
        auto state = job.snapshot();
        if (!job.running() && state.status != "idle" && !reported) {
            job.wait(); state = job.snapshot(); savePresentation(state, true, framesDuringJobs); printSummary(state); reported = true;
            std::cout << "UI frames during native job: " << framesDuringJobs << '\n';
            if (state.status == "complete" && state.summary.at("mode") != "regret" && state.summary.at("mode") != "policy_benchmark" && options.request.histogram) {
                try {
                    if (state.summary.contains("runs") && !state.summary.at("runs").empty()) {
                        for (const auto& row : state.summary.at("runs").back().at("native").at("results")) histograms.push_back(histogramFromResult(row));
                    }
                } catch (const std::exception& e) { histograms.clear(); histogramError = e.what(); }
            }
            if (state.status == "complete" && state.summary.at("mode") == "regret") { regretAnalysis = state.summary.at("analysis"); regretFocusFrames = 2; }
            if (state.status == "complete" && state.summary.at("mode") == "policy_benchmark") { policyAnalysis = state.summary.at("analysis"); policyFocusFrames = 2; }
            if (state.status == "complete" && state.summary.at("mode") == "detailed") {
                try { detailedView.load(state.summary.at("runs").back().at("native").at("details").at("path").get<std::string>()); }
                catch (const std::exception& e) { error = e.what(); }
            }
            if (options.benchmark) { done = true; continue; }
        }
        const bool busy = job.running();
        const bool liveTraining = submittedRequest.mode == "policy_benchmark" && submittedRequest.benchmarkAlgo == 1;
        const bool paused = job.running() && options.computePriority && !liveTraining;
        if (paused) {
            const std::string title = (submittedRequest.mode == "regret" || submittedRequest.mode == "policy_benchmark") ? "WoW Forever Optimizer / " + state.phase + " / Esc to cancel" : "WoW Forever Optimizer / Computing / Run " + std::to_string(state.run) + " / " + std::to_string(state.completedFights) + " fights / Esc to cancel";
            SDL_SetWindowTitle(window, title.c_str());
            SDL_Delay(50); continue; // No renderer frame, swap, or GPU work while native job runs.
        }
        SDL_SetWindowTitle(window, "WoW Forever Optimizer");
        const bool drawingDuringJob = job.running();
        ImGui_ImplOpenGL3_NewFrame(); ImGui_ImplSDL3_NewFrame(); ImGui::NewFrame();
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos); ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::Begin("Native compute", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        ImGui::TextColored(ImVec4(0.85f, 0.65f, 1.0f, 1.0f), "WOW FOREVER OPTIMIZER");
        ImGui::SameLine();
        ImGui::TextDisabled("| Warlock DPS Simulation & APL Studio");
        ImGui::Separator();

        // Top-level progress bar and status when running
        if (busy || pending || state.status == "complete" || !error.empty() || !state.error.empty()) {
            if (pending && !job.running()) ImGui::TextDisabled("Running / Escape to cancel");
            const float progress = state.totalFights ? std::min(1.0f, (static_cast<float>(state.run ? state.run - 1 : 0) + static_cast<float>(state.completedFights) / state.totalFights) / submittedRequest.runs) : 0;
            ImGui::ProgressBar(state.status == "complete" ? 1.0f : progress, ImVec2(-1, 0));
            ImGui::TextDisabled("Status: %s", state.status.c_str());
            if (!error.empty()) ImGui::TextColored(ImVec4(0.95f, 0.40f, 0.40f, 1.0f), "%s", error.c_str());
            if (!state.error.empty()) ImGui::TextColored(ImVec4(0.95f, 0.40f, 0.40f, 1.0f), "%s", state.error.c_str());
            ImGui::Separator();
        }

        const float totalAvailWidth = ImGui::GetContentRegionAvail().x;
        const float paneSpacing = ImGui::GetStyle().ItemSpacing.x;
        // Make left panel narrower than the right panel (~42% left vs ~58% right, clamped cleanly)
        const float desiredLeft = std::clamp(totalAvailWidth * 0.42f, 420.0f, 620.0f);
        const float leftWidth = std::min(desiredLeft, totalAvailWidth - 400.0f);
        const float rightWidth = std::max(350.0f, totalAvailWidth - paneSpacing - leftWidth);
        const float paneHeight = ImGui::GetContentRegionAvail().y;

        // LEFT PANEL: Character configuration, APL, and wowsims import
        ImGui::BeginChild("left_pane", ImVec2(leftWidth, paneHeight), false, ImGuiWindowFlags_None);
        editor->draw(busy);
        ImGui::EndChild();

        ImGui::SameLine();

        // RIGHT PANEL: Action tabs (Simulate, Analyze Regret, Policy Benchmark, Diagnostics)
        ImGui::BeginChild("right_pane", ImVec2(rightWidth, paneHeight), false, ImGuiWindowFlags_None);
        static int selectedRegretAction = 0;
        static int policyAlgo = static_cast<int>(options.request.benchmarkAlgo);
        static int evolutionGenerations = static_cast<int>(options.request.evolutionGenerations);
        static int evolutionPopulation = static_cast<int>(options.request.evolutionPopulation);
        static int evolutionSeed = static_cast<int>(options.request.evolutionSeed);
        static float evolutionSigma = options.request.evolutionSigma;
        static int benchmarkTrials = 8;
        static int policySeedStart = static_cast<int>(options.request.benchmarkSeedStart);
        static float learningRate = 2.0f;

        if (ImGui::BeginTabBar("right_tabs", ImGuiTabBarFlags_None)) {
            // TAB 1: Simulate
            ImGuiTabItemFlags simTabFlags = ImGuiTabItemFlags_None;
            ImGuiTabItemFlags regretTabFlags = ImGuiTabItemFlags_None;
            if (regretFocusFrames > 0) {
                regretTabFlags |= ImGuiTabItemFlags_SetSelected;
            }
            if (ImGui::BeginTabItem("Simulate", nullptr, simTabFlags)) {
                ImGui::SeparatorText("Simulation Settings");
                ImGui::BeginDisabled(busy);
                int total = static_cast<int>(options.request.totalFights), runs = static_cast<int>(options.request.runs);
                ImGui::PushItemWidth(170);
                if (ImGui::InputInt("Fights per run", &total, 10000, 100000)) options.request.totalFights = static_cast<uint32_t>(std::clamp(total, 10, 1048576));
                ImGui::SameLine();
                if (ImGui::InputInt("Repeat runs", &runs)) options.request.runs = static_cast<uint32_t>(std::clamp(runs, 1, 10));
                ImGui::PopItemWidth();
                ImGui::SameLine();
                uint64_t overallFights = static_cast<uint64_t>(options.request.totalFights) * options.request.runs;
                if (options.request.runs > 1) {
                    ImGui::TextDisabled("(Total across %u runs: %s fights)", options.request.runs,
                        overallFights >= 1000000 ? (std::to_string(overallFights / 1000000) + "M").c_str() : std::to_string(overallFights).c_str());
                } else {
                    ImGui::TextDisabled("(1 run of %s fights)",
                        overallFights >= 1000000 ? (std::to_string(overallFights / 1000000) + "M").c_str() : std::to_string(overallFights).c_str());
                }
                if (ImGui::CollapsingHeader("Advanced Settings")) {
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
                        submittedRequest = options.request; pending = true; error.clear();
                    } catch (const std::exception& e) { error = e.what(); }
                }
                ImGui::EndDisabled();
                ImGui::SameLine(); ImGui::BeginDisabled(!busy); if (ImGui::Button("Cancel")) { job.cancel(); } ImGui::EndDisabled();

                if (state.summary.contains("medians") && state.status == "complete") {
                    const auto& medians = state.summary.at("medians");
                    ImGui::SeparatorText("Results");
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.12f, 0.16f, 0.65f));
                    ImGui::BeginChild("results_card", ImVec2(0, 105), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
                    const auto& result = state.summary.at("runs").back().at("native");
                    if (!result.at("results").empty()) {
                        const auto& firstRow = result.at("results").front();
                        ImGui::TextColored(ImVec4(0.78f, 0.60f, 1.0f, 1.0f), "%.1f DPS", firstRow.at("meanDps").get<double>());
                        ImGui::SameLine();
                        ImGui::TextDisabled("(%s - %u fights)", firstRow.at("candidateId").get<std::string>().c_str(), firstRow.at("sampleCount").get<uint32_t>());
                    }
                    ImGui::Text("GPU: %.0f fights/sec | Native total: %.0f fights/sec (steady %.0f)",
                        medians.at("gpuSimsPerSecond").get<double>(),
                        medians.at("nativeSimsPerSecond").get<double>(),
                        medians.at("steadySimsPerSecond").get<double>());
                    ImGui::TextDisabled("C decode: %.3f ms | Native client: %.3f ms | Device: %s",
                        medians.at("cDecodeMs").get<double>(),
                        medians.at("clientWallMs").get<double>(),
                        result.at("probe").at("device").get<std::string>().c_str());
                    ImGui::EndChild();
                    ImGui::PopStyleColor();
                }
                if (!state.outputDirectory.empty()) ImGui::TextWrapped("Results: %s/summary.json", state.outputDirectory.c_str());

                ImGui::SeparatorText("DPS Distribution");
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

                ImGui::EndTabItem();
            }

            // TAB 2: Analyze Regret
            if (ImGui::BeginTabItem("Analyze Regret", nullptr, regretTabFlags)) {
                if (regretFocusFrames > 0) { ImGui::SetScrollHereY(0.0f); --regretFocusFrames; }
                ImGui::TextWrapped("Compare legal alternatives in fight #1, then run the current APL to the fight end. Plan depth 1 batches one-action comparisons; higher depths enable MCTS.");
                ImGui::BeginDisabled(busy || pending);
                if (frozenBenchmark) {
                    const char* candidates[] = {"Shadow Mastery / Ruin", "Affliction / Wrack", "Destruction", "Demonology / Shadow", "Demonology / Fire"};
                    int candidate = static_cast<int>(options.request.regretCandidate);
                    if (ImGui::Combo("Analyze build", &candidate, candidates, IM_ARRAYSIZE(candidates))) options.request.regretCandidate = candidate;
                }
                auto budget = [](const char* label, uint32_t& setting, int lo, int hi) {
                    int value = static_cast<int>(setting);
                    if (ImGui::InputInt(label, &value)) setting = static_cast<uint32_t>(std::clamp(value, lo, hi));
                };
                ImGui::SetNextItemWidth(120); budget("Decision roots", options.request.regretDecisions, 1, 128);
                ImGui::SameLine(); ImGui::SetNextItemWidth(120); budget("Search rounds", options.request.regretRounds, 1, 256);
                if (ImGui::TreeNode("Search settings")) {
                    budget("Plan depth", options.request.regretDepth, 1, 4);
                    budget("Search samples", options.request.regretSamples, 2, 256);
                    budget("Rollouts per alternative", options.request.regretValidation, 2, 4096);
                    ImGui::TreePop();
                }
                if (ImGui::Button("Analyze Session Regret", ImVec2(200, 0))) {
                    try {
                        JobRequest request = options.request;
                        if (frozenBenchmark) { request.suiteSource.clear(); request.suiteHash.clear(); request.candidateSource.clear(); }
                        else { editor->submit(request); request.regretCandidate = 0; }
                        request.mode = "regret"; request.runs = 1; request.rawReports = false;
                        submittedRequest = std::move(request);
                        selectedRegretAction = 0; pending = true; error.clear();
                    } catch (const std::exception& e) { error = e.what(); }
                }
                ImGui::EndDisabled();
                if (!regretAnalysis.is_null()) {
                    const auto& analysis = regretAnalysis;
                    const auto& rows = analysis.at("rows");
                    ImGui::Spacing();
                    ImGui::TextWrapped("%s", analysis.value("continuation", "Searched prefix then APL to terminal").c_str());
                    ImGui::Text("%s | Seed %u | %.1f DPS recorded | Baseline replay: exact",
                        analysis.at("candidateId").get<std::string>().c_str(), analysis.at("seed").get<uint32_t>(), analysis.at("baselineDps").get<double>());
                    ImGui::TextDisabled("Analyzed %u / %u eligible decisions (%u total); roots spread across the fight.",
                        analysis.at("analyzedDecisions").get<uint32_t>(), analysis.at("eligibleDecisions").get<uint32_t>(), analysis.at("totalDecisions").get<uint32_t>());
                    if (analysis.contains("rolloutFights")) ImGui::TextDisabled("%u rollouts in %u GPU batches; every rollout verified at fight end",
                        analysis.at("rolloutFights").get<uint32_t>(), analysis.at("rolloutDispatches").get<uint32_t>());
                    if (analysis.at("truncated").get<bool>()) ImGui::TextWrapped("Decision capture reached its limit; later decisions were omitted.");
                    ImGui::TextWrapped("Each rollout runs to the fight end. Red (+X.XX) indicates missed DPS vs best alternative. Green (-Y.YY) indicates the taken action was optimal, showing its lead over the runner-up alternative.");
                    if (rows.empty()) ImGui::TextDisabled("No decisions with multiple supported legal actions in this fight.");
                    if (ImGui::BeginTable("regret_table", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable)) {
                        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 60.0f);
                        ImGui::TableSetupColumn("Taken", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("Best Alternative", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("\xCE\x94 DPS (95% CI)", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("Inspect", ImGuiTableColumnFlags_WidthFixed, 64.0f);
                        ImGui::TableHeadersRow();
                        for (size_t i = 0; i < rows.size(); ++i) {
                            const auto& row = rows[i]; ImGui::PushID(static_cast<int>(i)); ImGui::TableNextRow();
                            const std::string taken = row.at("takenAction").get<std::string>();
                            const std::string best = row.at("bestAction").get<std::string>();
                            const double delta = row.at("dpsDelta").get<double>();
                            const double ciLow = row.at("ciLow").get<double>();
                            const double ciHigh = row.at("ciHigh").get<double>();
                            const bool isOptimal = (delta <= 0.001 && taken == best);

                            // Find next-best alternative if optimal
                            std::string runnerUpAction;
                            double runnerUpLead = 0.0;
                            if (isOptimal && row.contains("alternatives") && row.at("alternatives").is_array()) {
                                const auto& alts = row.at("alternatives");
                                for (const auto& alt : alts) {
                                    const std::string altAct = alt.at("action").get<std::string>();
                                    if (altAct != taken) {
                                        runnerUpAction = altAct;
                                        runnerUpLead = row.at("alternatives")[0].at("meanDps").get<double>() - alt.at("meanDps").get<double>();
                                        break;
                                    }
                                }
                            }

                            // 1. Time
                            ImGui::TableNextColumn(); ImGui::Text("%.2fs", row.at("time").get<double>());

                            // 2. Taken Action (Clean White)
                            ImGui::TableNextColumn();
                            if (isOptimal) {
                                ImGui::Text("%s", taken.c_str());
                                ImGui::SameLine();
                                ImGui::TextDisabled("(optimal)");
                            } else {
                                ImGui::Text("%s", taken.c_str());
                            }

                            // 3. Best Alternative (Clean White)
                            ImGui::TableNextColumn();
                            if (isOptimal) {
                                if (!runnerUpAction.empty()) ImGui::TextDisabled("Runner-up: %s", runnerUpAction.c_str());
                                else ImGui::TextDisabled("-");
                            } else {
                                ImGui::Text("%s", best.c_str());
                            }

                            // 4. Delta DPS with Condensed 95% Interval
                            ImGui::TableNextColumn();
                            if (isOptimal) {
                                if (!runnerUpAction.empty()) {
                                    ImGui::TextColored(ImVec4(0.40f, 0.85f, 0.55f, 1.0f), "-%.2f DPS", runnerUpLead);
                                } else {
                                    ImGui::TextColored(ImVec4(0.40f, 0.85f, 0.55f, 1.0f), "0.00 DPS");
                                }
                                ImGui::SameLine();
                                ImGui::TextDisabled("[%.1f, %.1f]", ciLow, ciHigh);
                            } else {
                                ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f), "+%.2f DPS", delta);
                                ImGui::SameLine();
                                ImGui::TextDisabled("[%.1f, %.1f]", ciLow, ciHigh);
                            }

                            // 5. Inspect button
                            ImGui::TableNextColumn();
                            if (ImGui::SmallButton("Inspect")) selectedRegretAction = static_cast<int>(i);
                            ImGui::PopID();
                        }
                        ImGui::EndTable();
                    }
                    if (selectedRegretAction >= 0 && static_cast<size_t>(selectedRegretAction) < rows.size()) {
                        const auto& row = rows[selectedRegretAction];
                        const std::string taken = row.at("takenAction").get<std::string>();
                        const std::string best = row.at("bestAction").get<std::string>();
                        const double delta = row.at("dpsDelta").get<double>();
                        const bool isOptimal = (delta <= 0.001 && taken == best);

                        ImGui::SeparatorText("Decision inspection");
                        ImGui::Text("Mana: %.0f | Shadow Trance: %s | %u paired future samples", row.at("mana").get<double>(),
                            row.at("shadowTrance").get<bool>() ? "active" : "inactive", row.at("samples").get<uint32_t>());

                        if (isOptimal) {
                            ImGui::TextColored(ImVec4(0.40f, 0.85f, 0.55f, 1.0f), "Action taken (%s) had the highest estimated DPS among tested alternatives.", taken.c_str());
                        } else {
                            ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f), "Suboptimal action taken (%s): missing +%.2f DPS vs %s",
                                taken.c_str(), delta, best.c_str());
                        }
                        ImGui::TextWrapped("Best plan: %s", row.at("plan").get<std::string>().c_str());
                        ImGui::TextWrapped("Taken-action comparison: %s", row.at("takenPlan").get<std::string>().c_str());
                        for (const auto& alternative : row.at("alternatives")) {
                            const std::string altAct = alternative.at("action").get<std::string>();
                            const double altDps = alternative.at("meanDps").get<double>();
                            if (altAct == taken && isOptimal) {
                                ImGui::TextColored(ImVec4(0.40f, 0.85f, 0.55f, 1.0f), "%.2f DPS: %s [Optimal choice]", altDps, alternative.at("plan").get<std::string>().c_str());
                            } else {
                                ImGui::TextWrapped("%.2f expected fight DPS: %s", altDps, alternative.at("plan").get<std::string>().c_str());
                            }
                            if (alternative.contains("doomDamage") && altAct == "Bane of Doom")
                                ImGui::TextDisabled("  Includes %.0f average Doom damage over the full fight", alternative.at("doomDamage").get<double>());
                        }
                    }
                }
                ImGui::EndTabItem();
            }

            // TAB 3: Policy Benchmark
            ImGuiTabItemFlags policyTabFlags = ImGuiTabItemFlags_None;
            if (policyFocusFrames > 0) {
                policyTabFlags |= ImGuiTabItemFlags_SetSelected;
                --policyFocusFrames;
            }
            if (ImGui::BeginTabItem("Policy Benchmark", nullptr, policyTabFlags)) {
                ImGui::TextWrapped("Compare complete policies against the original APL on matched evaluation seeds. Neuroevolution trains a small MLP by mutation and selection, with no MCTS.");
                const char* algos[] = {"MCTS + PUCT controller", "Neuroevolution MLP"};
                ImGui::SetNextItemWidth(230);
                if (ImGui::Combo("Algorithm", &policyAlgo, algos, IM_ARRAYSIZE(algos)))
                    benchmarkTrials = policyAlgo == 1 ? 2048 : 8;
                ImGui::SameLine();
                ImGui::SetNextItemWidth(110);
                ImGui::InputInt("Fight pairs", &benchmarkTrials, 1, 4);
                benchmarkTrials = std::clamp(benchmarkTrials, 2, policyAlgo == 1 ? 8192 : 64);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(110);
                if (policyAlgo == 0) ImGui::InputFloat("PUCT weight", &learningRate, 0.1f, 0.5f, "%.2f");
                if (policyAlgo == 1) {
                    ImGui::InputInt("Generations", &evolutionGenerations); evolutionGenerations = std::clamp(evolutionGenerations, 1, 16384);
                    ImGui::InputInt("Population", &evolutionPopulation); evolutionPopulation = std::clamp(evolutionPopulation, 2, 64);
                    ImGui::InputFloat("Mutation sigma", &evolutionSigma, 0.01f, 0.1f); evolutionSigma = std::clamp(evolutionSigma, 0.001f, 2.0f);
                    ImGui::InputInt("Evolution seed", &evolutionSeed); evolutionSeed = std::clamp(evolutionSeed, 1, 2147483646);
                }
                ImGui::SetNextItemWidth(110);
                ImGui::InputInt("Evaluation seed start", &policySeedStart, 64, 128);
                policySeedStart = std::clamp(policySeedStart, 0, 65536 - benchmarkTrials);
                ImGui::BeginDisabled(busy || pending);
                if (ImGui::Button("Run Policy Benchmark", ImVec2(180, 0))) {
                    try {
                        JobRequest request = options.request;
                        if (frozenBenchmark) { request.suiteSource.clear(); request.suiteHash.clear(); request.candidateSource.clear(); }
                        else { editor->submit(request); request.regretCandidate = 0; }
                        request.mode = "policy_benchmark";
                        request.runs = 1;
                        request.rawReports = false;
                        request.benchmarkTrials = static_cast<uint32_t>(benchmarkTrials);
                        request.benchmarkLearningRate = learningRate;
                        request.benchmarkSeedStart = static_cast<uint32_t>(policySeedStart);
                        request.benchmarkAlgo = static_cast<uint32_t>(policyAlgo);
                        request.evolutionGenerations = static_cast<uint32_t>(evolutionGenerations);
                        request.evolutionPopulation = static_cast<uint32_t>(evolutionPopulation);
                        request.evolutionSigma = evolutionSigma;
                        request.evolutionSeed = static_cast<uint32_t>(evolutionSeed);
                        submittedRequest = std::move(request);
                        pending = true;
                        error.clear();
                    } catch (const std::exception& e) { error = e.what(); }
                }
                ImGui::EndDisabled();
                ImGui::TextWrapped("Use a fresh evaluation seed range after tuning. The MLP uses time, mana, damage, legal actions and the current APL recommendation, with an APL score prior. Training runs separately from the fast simulator.");
                const auto& training = busy && submittedRequest.mode == "policy_benchmark" ? state.trainingCurve :
                    (policyAnalysis.contains("trainingCurve") ? policyAnalysis["trainingCurve"] : state.trainingCurve);
                if (training.is_array() && !training.empty()) {
                    std::vector<double> generations, apl, evolved, generationBest, generationMean, generationWorst;
                    for (const auto& point : training) {
                        generations.push_back(point.at("generation").get<double>());
                        apl.push_back(point.at("baselineDps").get<double>());
                        evolved.push_back(point.at("policyDps").get<double>());
                        generationBest.push_back(point.value("generationBestDps", evolved.back()));
                        generationMean.push_back(point.value("generationMeanDps", evolved.back()));
                        generationWorst.push_back(point.value("generationWorstDps", evolved.back()));
                    }
                    const auto trainingFights = training.back().value("trainingFights", 2u);
                    ImGui::Text("Generation %.0f: APL %.2f DPS; best retained MLP %.2f DPS (%u training fights each)", generations.back(), apl.back(), evolved.back(), trainingFights);
                    ImGui::Text("Retained training gain: %+.3f DPS (%+.3f%%)", evolved.back() - apl.back(),
                        apl.back() > 0 ? (evolved.back() - apl.back()) * 100 / apl.back() : 0);
                    static bool showMutations = false;
                    ImGui::Checkbox("Show mutation scores", &showMutations);
                    if (ImPlot::BeginPlot("Neuroevolution training", ImVec2(-1, 220))) {
                        const double baseline = apl.front();
                        const double low = showMutations ? std::min(baseline, *std::min_element(generationWorst.begin(), generationWorst.end())) : baseline;
                        const double high = std::max(baseline, *std::max_element(evolved.begin(), evolved.end()));
                        const double padding = showMutations ? std::max(1.0, std::max(baseline * 0.03, (high - low) * 0.15))
                            : std::max(0.05, (high - low) * 0.15);
                        ImPlot::SetupAxes("Generation", "Mean whole-fight DPS");
                        ImPlot::SetupAxisLimits(ImAxis_X1, 0, std::max(1.0, generations.back()), ImGuiCond_Always);
                        ImPlot::SetupAxisLimits(ImAxis_Y1, low - padding, high + padding, ImGuiCond_Always);
                        ImPlotSpec threshold;
                        threshold.LineColor = ImVec4(1.0f, 0.72f, 0.20f, 1.0f);
                        threshold.LineWeight = 4;
                        threshold.Flags = ImPlotInfLinesFlags_Horizontal;
                        ImPlot::PlotInfLines("APL on training seeds", &baseline, 1, threshold);
                        ImPlot::TagY(baseline, threshold.LineColor, "APL %.2f", baseline);
                        ImPlotSpec range;
                        range.FillColor = ImVec4(0.65f, 0.35f, 0.9f, 0.18f);
                        if (showMutations) ImPlot::PlotShaded("Generation range", generations.data(), generationWorst.data(), generationBest.data(), static_cast<int>(generations.size()), range);
                        ImPlotSpec current;
                        current.LineColor = ImVec4(0.2f, 0.85f, 1, 1);
                        current.Marker = ImPlotMarker_Circle;
                        if (showMutations) ImPlot::PlotLine("Generation mean", generations.data(), generationMean.data(), static_cast<int>(generations.size()), current);
                        current.LineColor = ImVec4(0.8f, 0.45f, 1, 1);
                        if (showMutations) ImPlot::PlotLine("Generation best", generations.data(), generationBest.data(), static_cast<int>(generations.size()), current);
                        current.LineColor = ImVec4(0.3f, 1, 0.45f, 1);
                        ImPlot::PlotLine("Best retained MLP", generations.data(), evolved.data(), static_cast<int>(evolved.size()), current);
                        ImPlot::EndPlot();
                    }
                    ImGui::Text("Current generation: mean %.2f; best %.2f; worst %.2f DPS", generationMean.back(), generationBest.back(), generationWorst.back());
                    if (training.back().contains("improvedCandidates"))
                        ImGui::Text("Mutants: %u improved; %u matched the previous best fitness",
                            training.back().value("improvedCandidates", 0u), training.back().value("equalFitnessCandidates", 0u));
                    ImGui::TextWrapped("Generation scores include new mutations only, so they can fall below APL. Best retained MLP includes the previous best and never decreases on these fixed training seeds.");
                    if (training.back().contains("elapsedTrainingMs")) {
                        const double seconds = training.back()["elapsedTrainingMs"].get<double>() / 1000.0;
                        ImGui::Text("Training elapsed: %.2fs; %.2f generations/s", seconds, seconds > 0 ? generations.back() / seconds : 0);
                    }
                    ImGui::TextWrapped("APL and each MLP use the same %u training seeds. Final paired results below use separate evaluation seeds. Training gains may not generalize.", trainingFights);
                }
                if (!policyAnalysis.is_null()) {
                    const auto& analysis = policyAnalysis;
                    const double delta = analysis.value("dpsDelta", 0.0);
                    const auto pairs = analysis.value("completedFightPairs", 0u);
                    if (ImGui::BeginTable("policy_benchmark_table", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                        ImGui::TableSetupColumn("Controller");
                        ImGui::TableSetupColumn("Mean whole-fight DPS");
                        ImGui::TableSetupColumn("Paired delta");
                        ImGui::TableHeadersRow();
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn(); ImGui::TextUnformatted("APL");
                        ImGui::TableNextColumn(); ImGui::Text("%.2f", analysis.value("meanBaselineDps", 0.0));
                        ImGui::TableNextColumn(); ImGui::TextDisabled("Baseline");
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn(); ImGui::TextUnformatted(analysis.value("algorithm", "Policy").c_str());
                        ImGui::TableNextColumn(); ImGui::Text("%.2f", analysis.value("meanAlgoDps", 0.0));
                        ImGui::TableNextColumn(); ImGui::Text("%+.2f DPS (%+.1f%%)", delta, analysis.value("dpsDeltaPct", 0.0));
                        ImGui::EndTable();
                    }
                    ImGui::Text("95%% paired interval: [%+.2f, %+.2f] DPS; standard error %.2f", analysis.value("ciLow", 0.0),
                        analysis.value("ciHigh", 0.0), analysis.value("standardError", 0.0));
                    ImGui::Text("%u complete fight pairs, seed start %u; %.1f%% of fights improved", pairs, analysis.value("fightIndex", 0u), analysis.value("winRatePct", 0.0));
                    if (pairs < 30) ImGui::TextWrapped("Small sample: use more fight pairs before concluding that one controller is better.");
                    if (analysis.contains("timing")) {
                        const auto& timing = analysis["timing"];
                        if (analysis.value("inferenceBackend", "") == "batched_gpu_full_fights") {
                            ImGui::Text("Neural full fights: %llu; dispatches: %llu; %.1f fights/s (startup excluded)",
                                static_cast<unsigned long long>(timing.value("neuralMeasuredFights", uint64_t(0))),
                                static_cast<unsigned long long>(timing.value("neuralDispatches", uint64_t(0))),
                                timing.value("neuralFightsPerSecond", 0.0));
                            ImGui::Text("Setup %.2fs; training %.2fs", timing.value("setupMs", 0.0) / 1000.0,
                                timing.value("trainingWallMs", 0.0) / 1000.0);
                            ImGui::Text("Neural GPU %.3fs; dispatch/transfer %.3fs; batch capacity %u fights",
                                timing.value("neuralMeasuredGpuMs", 0.0) / 1000.0,
                                std::max(0.0, timing.value("neuralMeasuredWallMs", 0.0) - timing.value("neuralMeasuredGpuMs", 0.0)) / 1000.0,
                                timing.value("neuralBatchCapacity", 0u));
                        }
                        ImGui::Text("Evaluation wall: %.2fs; search rollouts: %llu; committed decisions: %llu",
                            timing.value("evaluationWallMs", 0.0) / 1000.0,
                            static_cast<unsigned long long>(timing.value("searchRolloutFights", uint64_t(0))),
                            static_cast<unsigned long long>(analysis.value("totalDecisions", uint64_t(0))));
                    }
                    if (analysis.contains("timeline") && ImGui::CollapsingHeader("First controller fight: actual decisions")) {
                        ImGui::TextWrapped("APL recommendations below are at the controller's states, not moves from a separate baseline fight.");
                        ImGui::TextWrapped("Highlighted rows differ from the APL: cyan is executed, amber is recommended. A difference does not necessarily mean an improvement.");
                        if (ImGui::BeginTable("policy_actual_decisions", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(0, 220))) {
                            ImGui::TableSetupColumn("Time"); ImGui::TableSetupColumn("Mana");
                            ImGui::TableSetupColumn("Executed action"); ImGui::TableSetupColumn("APL would choose here");
                            ImGui::TableHeadersRow();
                            for (const auto& item : analysis["timeline"]) {
                                // Action zero delegates to the APL rather than overriding it.
                                const bool differs = item.contains("action") && item.contains("aplAction") &&
                                    item["action"] != 0 && item["action"] != item["aplAction"];
                                const auto executed = item.value("actionName", std::string("APL continuation"));
                                const auto recommended = item.value("aplActionName", std::string("APL continuation"));
                                ImGui::TableNextRow();
                                if (differs) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, IM_COL32(255, 185, 65, 24));
                                ImGui::TableNextColumn(); ImGui::Text("%.2fs", item.value("time", 0.0));
                                ImGui::TableNextColumn(); ImGui::Text("%.1f", item.value("mana", 0.0));
                                ImGui::TableNextColumn();
                                if (differs) ImGui::TextColored(ImVec4(0.35f, 0.85f, 1.0f, 1.0f), "%s (differs)", executed.c_str());
                                else ImGui::TextUnformatted(executed.c_str());
                                ImGui::TableNextColumn();
                                if (differs) ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.30f, 1.0f), "%s", recommended.c_str());
                                else ImGui::TextUnformatted(recommended.c_str());
                            }
                            ImGui::EndTable();
                        }
                    }
                } else ImGui::TextDisabled("No complete-fight policy results yet.");
                ImGui::EndTabItem();
            }

            // TAB 4: Diagnostics
            if (state.summary.contains("medians") && state.status == "complete") {
                if (ImGui::BeginTabItem("Diagnostics")) {
                    ImGui::TextDisabled("Phase: %s", state.phase.c_str());
                    ImGui::Separator();
                    ImGui::TextUnformatted(state.log.c_str());
                    ImGui::EndTabItem();
                }
            }

            ImGui::EndTabBar();
        }
        ImGui::EndChild();
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
            submittedRequest.presentation = options.computePriority && !(submittedRequest.mode == "policy_benchmark" && submittedRequest.benchmarkAlgo == 1) ? "rendering-paused" : "visible-10hz";
            try {
                detailedView.clear(); histograms.clear(); histogramCandidate = 0; histogramError.clear();
                job.start(submittedRequest); reported = false; framesDuringJobs = 0; error.clear();
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
    ImGui_ImplOpenGL3_Shutdown(); ImGui_ImplSDL3_Shutdown(); ImPlot::DestroyContext(); ImGui::DestroyContext();
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
