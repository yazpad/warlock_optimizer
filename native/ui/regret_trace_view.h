#pragma once
#include "native_job.h"
#include "imgui.h"
#include <fstream>
#include <map>
#include <cstdio>

namespace fastsim {
inline JobRequest regretTraceRequest(JobRequest request, const nlohmann::json& analysis,
                                     const nlohmann::json& row, const nlohmann::json& alternative) {
    using Json = nlohmann::json;
    namespace fs = std::filesystem;
    request.mode = "regret"; request.runs = 1; request.rawReports = false; request.histogram = false;
    request.outputDirectory.clear(); request.candidateSource.clear(); request.suiteSource.clear(); request.suiteHash.clear();
    auto read = [](const fs::path& path) { std::ifstream in(path); if (!in) throw std::runtime_error("Cannot read original suite"); return std::string(std::istreambuf_iterator<char>(in), {}); };
    std::string source = analysis.value("suiteSource", std::string());
    if (source.empty()) {
        // Older exports did not embed the suite. Recover only an artifact with the exact saved identity.
        std::error_code ec;
        for (fs::recursive_directory_iterator it(request.root / "artifacts", fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec)) {
            if (ec) { ec.clear(); continue; }
            if (it->path().filename() != "summary.json") continue;
            try {
                const auto summary = Json::parse(read(it->path()));
                if (summary.contains("analysis") && summary.at("analysis").value("suiteHash", "") == analysis.at("suiteHash").get<std::string>() && fs::exists(it->path().parent_path() / "suite.json")) {
                    source = read(it->path().parent_path() / "suite.json"); break;
                }
            } catch (const std::exception&) { /* Unrelated/incomplete artifact. */ }
        }
    }
    const auto suite = Json::parse(source.empty() ? read(request.root / "data/validation/forever_baseline.json") : source);
    bool found = false;
    for (size_t i = 0; i < suite.at("scenarios").size(); ++i) if (suite.at("scenarios")[i].at("id") == analysis.at("candidateId")) {
        request.regretCandidate = static_cast<uint32_t>(i); found = true; break;
    }
    if (!found || suite.at("seed") != analysis.at("seed")) throw std::runtime_error("Original build snapshot unavailable. Rerun regret analysis for this build to enable tracing.");
    if (suite.value("id", "") == "editor-warlock-v1") {
        request.suiteSource = source; request.suiteHash = analysis.at("suiteHash").get<std::string>();
    }
    Json selection;
    for (const char* key : {"suiteHash", "kernelHash", "detailKernelHash", "seed", "candidateId", "baselineDps"}) selection[key] = analysis.at(key);
    for (const char* key : {"decision", "time", "mana", "takenPlan"}) selection[key] = row.at(key);
    selection["alternativePlan"] = alternative.at("plan");
    request.regretTraceSource = selection.dump();
    return request;
}
inline void drawRegretTrace(const nlohmann::json& trace) {
    ImGui::SeparatorText("Trace");
    if (trace.value("schemaVersion", 1) < 2) {
        ImGui::TextDisabled("Older trace. Click Trace why to refresh."); return;
    }
    ImGui::Text("%+.2f DPS | 95%% CI [%.2f, %.2f] | %u pairs",
        trace.at("dpsDelta").get<double>(), trace.at("ciLow").get<double>(), trace.at("ciHigh").get<double>(), trace.at("samples").get<unsigned>());
    const auto& drivers = trace.at("drivers");
    if (drivers.empty()) { ImGui::TextUnformatted("No damage difference detected."); }
    else {
        static int selected = 0;
        static std::string previous;
        const std::string key = std::to_string(trace.at("decision").get<unsigned>()) + trace.at("alternativePlan").get<std::string>();
        if (key != previous || selected >= static_cast<int>(drivers.size())) { selected = 0; previous = key; }
        if (ImGui::BeginCombo("Contributor", drivers[selected].at("name").get<std::string>().c_str())) {
            for (size_t i = 0; i < drivers.size(); ++i) {
                char label[160]; std::snprintf(label, sizeof(label), "%s (%+.2f DPS)", drivers[i].at("name").get<std::string>().c_str(), drivers[i].at("dpsDelta").get<double>());
                if (ImGui::Selectable(label, selected == static_cast<int>(i))) selected = static_cast<int>(i);
            }
            ImGui::EndCombo();
        }
        const auto& driver = drivers[selected];
        ImGui::Text("%+.2f DPS | %+.2f casts | %+.2f ticks",
            driver.at("dpsDelta").get<double>(), driver.at("castsDelta").get<double>(), driver.at("ticksDelta").get<double>());
        if (ImGui::CollapsingHeader("Damage details")) {
            if (ImGui::BeginTable("trace_damage", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                for (const char* label : {"Metric", "Taken", "Alternative"}) ImGui::TableSetupColumn(label);
                ImGui::TableHeadersRow();
                auto metric = [](const char* label, double a, double b) {
                    ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(label);
                    ImGui::TableNextColumn(); ImGui::Text("%.2f", a); ImGui::TableNextColumn(); ImGui::Text("%.2f", b);
                };
                const auto& a = driver.at("taken"); const auto& b = driver.at("alternative");
                for (const char* label : {"damage", "casts", "ticks", "hits", "crits", "misses"}) metric(label, a.value(label, 0.0), b.value(label, 0.0));
                const double ah = a.value("hits", 0.0), bh = b.value("hits", 0.0);
                if (ah > 0 && bh > 0) metric("Damage / landed event", a.value("damage", 0.0) / ah, b.value("damage", 0.0) / bh);
                if (driver.contains("takenUptime") || driver.contains("alternativeUptime")) metric("Uptime (s)", driver.value("takenUptime", 0.0), driver.value("alternativeUptime", 0.0));
                ImGui::EndTable();
            }
        }
        const std::string id = std::to_string(driver.at("actionId").get<unsigned>());
        if (id == "0") ImGui::TextDisabled("No player action trace for this damage source.");
        else {
            if (ImGui::CollapsingHeader("Decision checks")) {
                ImGui::TextDisabled("Checks per fight; cooldown, mana, then APL. Not damage attribution.");
                std::map<std::string, std::pair<double, double>> counts;
                for (const char* side : {"taken", "alternative"}) {
                    const auto& gates = driver.at("gates").at(side);
                    for (auto it = gates.begin(); it != gates.end(); ++it) {
                        if (std::string(side) == "taken") counts[it.key()].first = it.value().get<double>();
                        else counts[it.key()].second = it.value().get<double>();
                    }
                }
                if (ImGui::BeginTable("trace_checks", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                    for (const char* label : {"Check", "Taken", "Alternative"}) ImGui::TableSetupColumn(label);
                    ImGui::TableHeadersRow();
                    for (const auto& [name, values] : counts) {
                        ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(name.c_str());
                        ImGui::TableNextColumn(); ImGui::Text("%.2f", values.first); ImGui::TableNextColumn(); ImGui::Text("%.2f", values.second);
                    }
                    ImGui::EndTable();
                }
            }
            if (ImGui::CollapsingHeader("Cast timing")) {
                ImGui::TextDisabled("Example replay. Opposite-branch checks are from its listed decision time.");
                for (const char* side : {"taken", "alternative"}) {
                    ImGui::PushID(side); ImGui::TextUnformatted(side);
                    if (ImGui::BeginTable("trace_windows", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY, ImVec2(0, 200))) {
                        for (const char* label : {"Cast at", "Effect left", "Power / haste", "Other action", "Other at", "Other check"}) ImGui::TableSetupColumn(label);
                        ImGui::TableHeadersRow();
                        for (const auto& window : driver.at("windows").at(side)) {
                            const auto& e = window.at("event"); const auto& other = window.at("other");
                            ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("%.2fs", e.at("time").get<double>());
                            ImGui::TableNextColumn(); ImGui::Text("%.2fs", e.at("remaining").value(id, 0.0));
                            ImGui::TableNextColumn(); ImGui::Text("%.0f / %.2fx", e.at("power").get<double>(), e.at("haste").get<double>());
                            ImGui::TableNextColumn(); ImGui::TextUnformatted(other.at("action").get<std::string>().c_str());
                            ImGui::TableNextColumn(); ImGui::Text("%.2fs", other.at("time").get<double>());
                            ImGui::TableNextColumn(); ImGui::TextUnformatted(other.at("gates").value(id, "Unavailable").c_str());
                        }
                        ImGui::EndTable();
                    }
                    ImGui::PopID();
                }
            }
        }
    }
    const auto& example = trace.at("example");
    if (example.contains("firstChangedDecision")) {
        const auto& change = example.at("firstChangedDecision");
        ImGui::TextWrapped("First change: %s at %.2fs -> %s at %.2fs.",
            change.at("taken").at("action").get<std::string>().c_str(), change.at("taken").at("time").get<double>(),
            change.at("alternative").at("action").get<std::string>().c_str(), change.at("alternative").at("time").get<double>());
    }
    ImGui::TextDisabled("Example %u: median DPS difference. Observed changes, not proven causes.", example.at("sample").get<unsigned>());
    if (ImGui::CollapsingHeader("Action timelines")) {
        for (const char* side : {"taken", "alternative"}) {
            ImGui::PushID(side); ImGui::TextUnformatted(side);
            if (ImGui::BeginTable("trace_timeline", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable, ImVec2(0, 240))) {
                for (const char* label : {"Time", "Action", "Rule", "Mana", "RNG draws"}) ImGui::TableSetupColumn(label);
                ImGui::TableHeadersRow();
                for (const auto& e : example.at(side).at("timeline")) {
                    ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::Text("%.2fs", e.at("time").get<double>());
                    ImGui::TableNextColumn(); ImGui::TextUnformatted(e.at("action").get<std::string>().c_str());
                    ImGui::TableNextColumn();
                    if (e.value("forced", false)) ImGui::TextUnformatted("Forced");
                    else if (e.at("rule").get<unsigned>()) ImGui::Text("%u", e.at("rule").get<unsigned>());
                    else ImGui::TextUnformatted("Fallback");
                    ImGui::TableNextColumn(); ImGui::Text("%.0f", e.at("mana").get<double>());
                    ImGui::TableNextColumn(); ImGui::Text("%u", e.at("rngCalls").get<unsigned>());
                }
                ImGui::EndTable();
            }
            ImGui::PopID();
        }
        ImGui::TextWrapped("RNG consumption differs in %u/%u pairs; shared seeds do not guarantee matching procs.", trace.at("randomConsumptionDivergedPairs").get<unsigned>(), trace.at("samples").get<unsigned>());
    }
}
}
