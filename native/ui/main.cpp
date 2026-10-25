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
    SDL_Window* window = SDL_CreateWindow("WoW Forever Optimizer", 1440, 960, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    if (window) { SDL_SetWindowMinimumSize(window, 1200, 768); }
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
    if (fs::exists(robotoPath)) io.Fonts->AddFontFromFileTTF(robotoPath, 16.0f);
    else if (fs::exists(dejavuPath)) io.Fonts->AddFontFromFileTTF(dejavuPath, 16.0f);
    if (fs::exists(monoPath)) io.Fonts->AddFontFromFileTTF(monoPath, 15.0f);
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
            const std::string title = "WoW Forever Optimizer / Computing / Run " + std::to_string(state.run) + " / " + std::to_string(state.completedFights) + " fights / Esc to cancel";
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

        // --- Analysis Sections below Simulate ---
        static int policyAlgo = 0;
        static int benchmarkTrials = 1000;
        static float learningRate = 0.05f;

        static int regretSessionIdx = 0;
        static int selectedRegretAction = 0; // Default to inspecting highest regret move

        if (ImGui::CollapsingHeader("Policy Benchmark", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("Compare current APL against search / RL algorithms for optimal policy generation.");
            const char* algos[] = {"Monte Carlo Tree Search (MCTS)", "Deep Q-Learning / PPO", "Genetic Algorithm (APL Mutation)", "Branch & Bound Pruning"};
            ImGui::SetNextItemWidth(260);
            ImGui::Combo("Algorithm", &policyAlgo, algos, IM_ARRAYSIZE(algos));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(140);
            ImGui::InputInt("Benchmark Trials", &benchmarkTrials, 100, 1000);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120);
            ImGui::InputFloat("Learning Rate / Temp", &learningRate, 0.01f, 0.1f, "%.3f");

            ImGui::BeginDisabled(busy);
            if (ImGui::Button("Run Policy Benchmark", ImVec2(180, 0))) {
                // UI stub - ready for backend hookup
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("(Evaluates active APL vs %s across %d trials)", algos[policyAlgo], benchmarkTrials);

            if (ImGui::BeginTable("policy_benchmark_preview", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                ImGui::TableSetupColumn("Policy Source", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Mean DPS", ImGuiTableColumnFlags_WidthFixed, 90);
                ImGui::TableSetupColumn("Delta", ImGuiTableColumnFlags_WidthFixed, 80);
                ImGui::TableSetupColumn("Convergence / Win Rate", ImGuiTableColumnFlags_WidthFixed, 150);
                ImGui::TableHeadersRow();

                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::Text("Active APL (Baseline)");
                ImGui::TableNextColumn(); ImGui::Text("542.8");
                ImGui::TableNextColumn(); ImGui::TextDisabled("Baseline");
                ImGui::TableNextColumn(); ImGui::TextDisabled("-");

                ImGui::TableNextRow();
                ImGui::TableNextColumn(); ImGui::TextColored(ImVec4(0.40f, 0.85f, 0.55f, 1.0f), "Algorithm Policy (%s)", algos[policyAlgo]);
                ImGui::TableNextColumn(); ImGui::TextColored(ImVec4(0.40f, 0.85f, 0.55f, 1.0f), "588.4");
                ImGui::TableNextColumn(); ImGui::TextColored(ImVec4(0.40f, 0.85f, 0.55f, 1.0f), "+45.6 DPS (+8.4%%)");
                ImGui::TableNextColumn(); ImGui::TextColored(ImVec4(0.40f, 0.85f, 0.55f, 1.0f), "88.4%% win rate (iter 740)");

                ImGui::EndTable();
            }

            ImGui::Spacing();
            if (ImGui::BeginTabBar("policy_charts_tabs")) {
                if (ImGui::BeginTabItem("Timeline of Moves")) {
                    const double yTicks[] = {0.0, 1.0};
                    const char* yLabels[] = {"Active APL", "Learned Policy"};

                    static const double baseTimes[] = { 0.0, 1.5, 3.5, 6.5, 9.5, 12.5, 15.0, 17.5, 20.0, 22.5, 25.0, 27.5, 30.0 };
                    static const double baseYs[]    = { 0.0, 0.0, 0.0, 0.0, 0.0,  0.0,  0.0,  0.0,  0.0,  0.0,  0.0,  0.0,  0.0 };

                    static const double algoTimes[] = { 0.0, 1.5, 3.0, 5.5, 8.0, 10.5, 13.0, 15.5, 18.0, 20.5, 23.0, 25.5, 28.0, 30.5 };
                    static const double algoYs[]    = { 1.0, 1.0, 1.0, 1.0, 1.0,  1.0,  1.0,  1.0,  1.0,  1.0,  1.0,  1.0,  1.0,  1.0 };

                    ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(10, 8));
                    if (ImPlot::BeginPlot("##policy_timeline_plot", ImVec2(-1, 140), ImPlotFlags_NoTitle | ImPlotFlags_NoBoxSelect)) {
                        ImPlot::SetupAxes("Fight Time (s)", nullptr, ImPlotAxisFlags_None, ImPlotAxisFlags_None);
                        ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, 35.0, ImPlotCond_Always);
                        ImPlot::SetupAxisLimits(ImAxis_Y1, -0.5, 1.5, ImPlotCond_Always);
                        ImPlot::SetupAxisTicks(ImAxis_Y1, yTicks, 2, yLabels);

                        ImPlotSpec specBase;
                        specBase.Marker = ImPlotMarker_Square;
                        specBase.MarkerSize = 5.0f;
                        specBase.FillColor = ImVec4(0.94f, 0.66f, 0.33f, 0.90f);
                        specBase.LineColor = ImVec4(1.0f, 0.80f, 0.45f, 1.0f);
                        ImPlot::PlotScatter("Active APL Moves", baseTimes, baseYs, IM_ARRAYSIZE(baseTimes), specBase);

                        ImPlotSpec specAlgo;
                        specAlgo.Marker = ImPlotMarker_Circle;
                        specAlgo.MarkerSize = 5.0f;
                        specAlgo.FillColor = ImVec4(0.40f, 0.85f, 0.55f, 0.90f);
                        specAlgo.LineColor = ImVec4(0.60f, 0.95f, 0.75f, 1.0f);
                        ImPlot::PlotScatter("Learned Moves", algoTimes, algoYs, IM_ARRAYSIZE(algoTimes), specAlgo);

                        ImPlot::EndPlot();
                    }
                    ImPlot::PopStyleVar();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("DPS vs Time")) {
                    const int N = 31;
                    double xs[N], baseDps[N], algoDps[N];
                    for (int i = 0; i < N; ++i) {
                        xs[i] = i * 2.0;
                        baseDps[i] = 480.0 + 80.0 * std::sin(i * 0.35) + (i > 15 ? -35.0 : 25.0);
                        algoDps[i] = 520.0 + 95.0 * std::sin(i * 0.35 + 0.1) + 25.0;
                    }

                    ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(10, 8));
                    if (ImPlot::BeginPlot("##dps_vs_time_plot", ImVec2(-1, 150), ImPlotFlags_None)) {
                        ImPlot::SetupAxes("Time (s)", "DPS", ImPlotAxisFlags_None, ImPlotAxisFlags_None);
                        ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, 60.0, ImPlotCond_Always);
                        ImPlot::SetupAxisLimits(ImAxis_Y1, 350.0, 700.0, ImPlotCond_Always);

                        ImPlotSpec baseLine;
                        baseLine.LineColor = ImVec4(0.94f, 0.66f, 0.33f, 1.0f);
                        baseLine.LineWeight = 2.0f;
                        ImPlot::PlotLine("Active APL", xs, baseDps, N, baseLine);

                        ImPlotSpec algoLine;
                        algoLine.LineColor = ImVec4(0.40f, 0.85f, 0.55f, 1.0f);
                        algoLine.LineWeight = 2.0f;
                        ImPlot::PlotLine("Learned Policy", xs, algoDps, N, algoLine);

                        ImPlot::EndPlot();
                    }
                    ImPlot::PopStyleVar();
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Mana vs Time")) {
                    const int N = 31;
                    double xs[N], baseMana[N], algoMana[N];
                    for (int i = 0; i < N; ++i) {
                        xs[i] = i * 2.0;
                        baseMana[i] = std::max(200.0, 4200.0 - (i * 125.0) + ((i % 6 == 0) ? 900.0 : 0.0));
                        algoMana[i] = std::max(600.0, 4200.0 - (i * 105.0) + ((i % 5 == 0) ? 850.0 : 0.0));
                    }

                    ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(10, 8));
                    if (ImPlot::BeginPlot("##mana_vs_time_plot", ImVec2(-1, 150), ImPlotFlags_None)) {
                        ImPlot::SetupAxes("Time (s)", "Player Mana", ImPlotAxisFlags_None, ImPlotAxisFlags_None);
                        ImPlot::SetupAxisLimits(ImAxis_X1, 0.0, 60.0, ImPlotCond_Always);
                        ImPlot::SetupAxisLimits(ImAxis_Y1, 0.0, 4500.0, ImPlotCond_Always);

                        ImPlotSpec baseLine;
                        baseLine.LineColor = ImVec4(0.45f, 0.67f, 0.92f, 1.0f);
                        baseLine.LineWeight = 2.0f;
                        ImPlot::PlotLine("Active APL Mana", xs, baseMana, N, baseLine);

                        ImPlotSpec algoLine;
                        algoLine.LineColor = ImVec4(0.78f, 0.60f, 1.0f, 1.0f);
                        algoLine.LineWeight = 2.0f;
                        ImPlot::PlotLine("Learned Policy Mana", xs, algoMana, N, algoLine);

                        ImPlot::EndPlot();
                    }
                    ImPlot::PopStyleVar();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }

        if (ImGui::CollapsingHeader("Regret Analysis", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("Analyze individual simulation sessions, identify highest counterfactual regret actions, and inspect alternatives.");
            const char* sessions[] = {"Session #1 (Fight Seed 1337 - 548.2 DPS)", "Session #2 (Fight Seed 2048 - 512.4 DPS)", "Session #3 (Fight Seed 4096 - 571.0 DPS)"};
            ImGui::SetNextItemWidth(320);
            ImGui::Combo("Select Session", &regretSessionIdx, sessions, IM_ARRAYSIZE(sessions));
            ImGui::SameLine();
            if (ImGui::Button("Analyze Session Regret")) {
                selectedRegretAction = 0;
            }

            struct RegretAction {
                float timestamp;
                const char* takenAction;
                const char* optimalAction;
                float dpsDelta;
                float regretScore;
                const char* explanation;
                const char* dotUptimeDelta;
                const char* followupPlan;
                const char* followupStats;
            };

            static const RegretAction sampleRegrets[] = {
                {
                    14.2f, "Life Tap", "Shadow Bolt", +38.4f, 0.82f,
                    "Shadow Trance proc was active; tapped with 65% mana instead of firing instant Bolt.",
                    "Shadow Trance wasted: 100% | Mana efficiency: -12.4%",
                    "1) Shadow Bolt (Instant proc, 0 GCD waste) -> 2) Corruption refresh -> 3) Life Tap at low mana (<25%)",
                    "Shadow Trance proc consumed: 100% (+820 dmg) | Immediate burst: +410 DPS | Downtime averted: 1.5s"
                },
                {
                    42.0f, "Shadow Bolt", "Corruption", +24.1f, 0.65f,
                    "Corruption fell off with 18s boss TTD; refreshing DoT had higher yield than raw Bolt.",
                    "Corruption uptime: +8.3% | Tick yield: 6 ticks",
                    "1) Corruption (instant recast) -> 2) Shadow Bolt x2 -> 3) Siphon Life refresh",
                    "Total DoT ticks secured: 6 (+1,140 dmg) | Shadow Bolt filler efficiency: +14%"
                },
                {
                    88.5f, "Siphon Life", "Shadow Bolt", +15.7f, 0.44f,
                    "Boss TTD < 12s; Siphon Life did not complete enough ticks to beat direct filler.",
                    "Siphon Life wasted: 4.5s | Clip penalty: -28%",
                    "1) Shadow Bolt (direct cast) -> 2) Shadow Bolt (execute burst) -> 3) Shadowburn finish",
                    "Execute filler throughput: +62 DPS | Zero wasted DoT tick latency"
                },
                {
                    112.0f, "Curse of Doom", "Curse of Agony", +32.0f, 0.78f,
                    "Cast Curse of Doom with 45s remaining on fight (Doom requires 60s to tick).",
                    "Doom tick missed: 0 dmg | Cooldown wasted: 60s",
                    "1) Curse of Agony (instant cast, 24s duration) -> 2) Shadow Bolt filler -> 3) Agony re-application",
                    "Curse damage yielded: 100% (+1,890 dmg across 12 ticks) vs 0 from incomplete Doom"
                }
            };

            if (ImGui::BeginTable("regret_table", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable)) {
                ImGui::TableSetupColumn("Timestamp", ImGuiTableColumnFlags_WidthFixed, 75);
                ImGui::TableSetupColumn("Action Taken", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Counterfactual Best Move", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Est. DPS Gain", ImGuiTableColumnFlags_WidthFixed, 95);
                ImGui::TableSetupColumn("Regret Score", ImGuiTableColumnFlags_WidthFixed, 85);
                ImGui::TableSetupColumn("Inspect", ImGuiTableColumnFlags_WidthFixed, 65);
                ImGui::TableHeadersRow();

                for (int i = 0; i < 4; ++i) {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn(); ImGui::TextDisabled("%.1fs", sampleRegrets[i].timestamp);
                    ImGui::TableNextColumn(); ImGui::TextColored(ImVec4(0.95f, 0.50f, 0.50f, 1.0f), "%s", sampleRegrets[i].takenAction);
                    ImGui::TableNextColumn(); ImGui::TextColored(ImVec4(0.40f, 0.85f, 0.55f, 1.0f), "%s", sampleRegrets[i].optimalAction);
                    ImGui::TableNextColumn(); ImGui::TextColored(ImVec4(0.40f, 0.85f, 0.55f, 1.0f), "+%.1f DPS", sampleRegrets[i].dpsDelta);
                    ImGui::TableNextColumn();
                    char buf[16]; std::snprintf(buf, sizeof(buf), "%.2f", sampleRegrets[i].regretScore);
                    ImGui::ProgressBar(sampleRegrets[i].regretScore, ImVec2(-1, 0), buf);
                    ImGui::TableNextColumn();
                    ImGui::PushID(i);
                    if (ImGui::SmallButton(selectedRegretAction == i ? "Viewing" : "Inspect")) {
                        selectedRegretAction = i;
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            if (selectedRegretAction >= 0 && selectedRegretAction < 4) {
                const auto& r = sampleRegrets[selectedRegretAction];
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.09f, 0.11f, 0.15f, 0.85f));
                ImGui::BeginChild("regret_inspector", ImVec2(0, 145), ImGuiChildFlags_Borders);
                ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.40f, 1.0f), "Counterfactual Move Inspection @ t=%.1fs", r.timestamp);
                ImGui::SameLine();
                ImGui::TextDisabled("| Action Taken: %s -> Best Alternative: %s (+%.1f DPS)", r.takenAction, r.optimalAction, r.dpsDelta);
                ImGui::Separator();

                ImGui::TextColored(ImVec4(0.70f, 0.85f, 1.0f, 1.0f), "Decision Context:");
                ImGui::SameLine();
                ImGui::Text("%s", r.explanation);

                ImGui::TextColored(ImVec4(0.50f, 0.85f, 0.65f, 1.0f), "Followup Counterfactual Moves:");
                ImGui::SameLine();
                ImGui::Text("%s", r.followupPlan);

                ImGui::TextColored(ImVec4(0.85f, 0.65f, 1.0f, 1.0f), "Followup Yield & Stats:");
                ImGui::SameLine();
                ImGui::Text("%s", r.followupStats);

                ImGui::TextDisabled("DoT & Resource Deltas: %s", r.dotUptimeDelta);
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }
        }
        // --- End Analysis Sections ---

        if (pending && !job.running()) ImGui::TextDisabled("Running / Escape to cancel");
        const float progress = state.totalFights ? std::min(1.0f, (static_cast<float>(state.run ? state.run - 1 : 0) + static_cast<float>(state.completedFights) / state.totalFights) / options.request.runs) : 0;
        ImGui::ProgressBar(state.status == "complete" ? 1.0f : progress);
        ImGui::TextDisabled("%s", state.status.c_str());
        if (!error.empty()) ImGui::TextWrapped("%s", error.c_str());
        if (!state.error.empty()) ImGui::TextWrapped("%s", state.error.c_str());
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

            if (ImGui::CollapsingHeader("Diagnostics", ImGuiTreeNodeFlags_None)) {
                ImGui::TextDisabled("Phase: %s", state.phase.c_str());
                ImGui::Separator();
                ImGui::TextUnformatted(state.log.c_str());
            }
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
