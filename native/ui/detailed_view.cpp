#include "detailed_view.h"
#include "imgui.h"
#include "implot.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <stdexcept>
using Json = nlohmann::json;
namespace fastsim {
void DetailedView::clear() { data_ = nullptr; visible_.clear(); candidate_ = 0; }
void DetailedView::load(const std::filesystem::path& path) {
    if (std::filesystem::file_size(path) > 32 * 1024 * 1024) throw std::runtime_error("Detailed analysis is too large");
    std::ifstream input(path); auto data = Json::parse(input);
    if (data.at("schemaVersion") != 1 || data.at("mode") != "detailed" || !data.at("candidates").is_array() || data.at("candidates").empty() || data.at("candidates").size() > 5)
        throw std::runtime_error("Invalid detailed analysis");
    for (const auto& candidate : data.at("candidates")) {
        const auto& trace = candidate.at("timeline");
        if (trace.at("reportReplay") != "bit-identical" || trace.at("events").size() > 8192 || trace.at("recordCount") != trace.at("events").size())
            throw std::runtime_error("Invalid detailed timeline");
    }
    data_ = std::move(data); candidate_ = 0; from_ = 0; until_ = data_["candidates"][0]["duration"].get<float>();
    search_[0] = 0; damageOnly_ = false; filter();
}
void DetailedView::filter() {
    visible_.clear(); if (data_.is_null()) return;
    auto lower = [](std::string text) { std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); }); return text; };
    const std::string needle = lower(search_);
    const auto& events = data_["candidates"][candidate_]["timeline"]["events"];
    for (size_t i = 0; i < events.size(); ++i) {
        const auto& event = events[i]; const double time = event.at("time");
        if (time < from_ || time > until_ || (damageOnly_ && event.at("damage").get<double>() <= 0)) continue;
        const auto text = event.at("spell").get<std::string>() + " " + event.at("event").get<std::string>() + " " + event.at("actor").get<std::string>();
        if (!needle.empty() && lower(text).find(needle) == std::string::npos) continue;
        visible_.push_back(i);
    }
}
void DetailedView::drawTimeline() {
    const auto& build = data_["candidates"][candidate_]; const auto& timeline = build["timeline"];
    ImGui::TextDisabled("Fight #1 / seed %u / %zu events", timeline["seed"].get<uint32_t>(), timeline["events"].size());
    if (timeline["truncated"].get<bool>()) ImGui::TextColored(ImVec4(1, .7f, .3f, 1), "Truncated: first %u of %u events", timeline["recordCount"].get<unsigned>(), timeline["totalRecords"].get<unsigned>());
    bool changed = false;
    ImGui::PushItemWidth(220);
    changed |= ImGui::InputTextWithHint("##event-search", "Spell or event", search_, sizeof(search_));
    ImGui::SameLine(); changed |= ImGui::Checkbox("Damage only", &damageOnly_);
    changed |= ImGui::DragFloatRange2("Seconds", &from_, &until_, .1f, 0, build["duration"].get<float>(), "%.2f", "%.2f");
    ImGui::PopItemWidth();
    if (changed) {
        const float duration = build["duration"];
        if (!std::isfinite(from_)) from_ = 0;
        if (!std::isfinite(until_)) until_ = duration;
        from_ = std::clamp(from_, 0.0f, duration); until_ = std::clamp(until_, from_, duration); filter();
    }
    const auto& events = timeline["events"];
    std::vector<double> playerX, playerY, petX, petY, sysX, sysY;
    std::vector<size_t> playerIdx, petIdx, sysIdx;
    for (const size_t index : visible_) {
        const auto& event = events[index]; const std::string actor = event["actor"];
        const double t = event["time"].get<double>();
        if (actor == "Player") { playerX.push_back(t); playerY.push_back(0.0); playerIdx.push_back(index); }
        else if (actor == "Pet") { petX.push_back(t); petY.push_back(1.0); petIdx.push_back(index); }
        else { sysX.push_back(t); sysY.push_back(2.0); sysIdx.push_back(index); }
    }
    const double yTicks[] = {0.0, 1.0, 2.0};
    const char* yLabels[] = {"Player", "Pet", "System"};
    const Json* hovered = nullptr;
    ImPlot::PushStyleVar(ImPlotStyleVar_PlotPadding, ImVec2(10, 8));
    if (ImPlot::BeginPlot("##event_timeline_plot", ImVec2(-1, 130), ImPlotFlags_NoTitle | ImPlotFlags_NoBoxSelect)) {
        ImPlot::SetupAxes("Time (s)", nullptr, ImPlotAxisFlags_None, ImPlotAxisFlags_None);
        ImPlot::SetupAxisLimits(ImAxis_X1, from_, until_, ImPlotCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, -0.6, 2.6, ImPlotCond_Always);
        ImPlot::SetupAxisTicks(ImAxis_Y1, yTicks, 3, yLabels);

        ImPlotSpec specPlayer;
        specPlayer.Marker = ImPlotMarker_Square;
        specPlayer.MarkerSize = 4.0f;
        specPlayer.FillColor = ImVec4(0.94f, 0.66f, 0.33f, 0.90f);
        specPlayer.LineColor = ImVec4(1.0f, 0.80f, 0.45f, 1.0f);
        ImPlot::PlotScatter("Player##lane", playerX.data(), playerY.data(), static_cast<int>(playerX.size()), specPlayer);

        ImPlotSpec specPet;
        specPet.Marker = ImPlotMarker_Circle;
        specPet.MarkerSize = 4.0f;
        specPet.FillColor = ImVec4(0.41f, 0.80f, 0.59f, 0.90f);
        specPet.LineColor = ImVec4(0.60f, 0.95f, 0.75f, 1.0f);
        ImPlot::PlotScatter("Pet##lane", petX.data(), petY.data(), static_cast<int>(petX.size()), specPet);

        ImPlotSpec specSys;
        specSys.Marker = ImPlotMarker_Diamond;
        specSys.MarkerSize = 4.0f;
        specSys.FillColor = ImVec4(0.45f, 0.67f, 0.92f, 0.90f);
        specSys.LineColor = ImVec4(0.65f, 0.82f, 1.0f, 1.0f);
        ImPlot::PlotScatter("System##lane", sysX.data(), sysY.data(), static_cast<int>(sysX.size()), specSys);

        if (ImPlot::IsPlotHovered()) {
            const ImPlotPoint mouse = ImPlot::GetPlotMousePos();
            const double thresholdX = std::max(0.1, (until_ - from_) * 0.015);
            double nearestDist = thresholdX;
            auto checkLane = [&](const std::vector<double>& xs, double targetY, const std::vector<size_t>& indices) {
                if (std::abs(mouse.y - targetY) < 0.45) {
                    for (size_t i = 0; i < xs.size(); ++i) {
                        const double dist = std::abs(mouse.x - xs[i]);
                        if (dist < nearestDist) {
                            nearestDist = dist;
                            hovered = &events[indices[i]];
                        }
                    }
                }
            };
            checkLane(playerX, 0.0, playerIdx);
            checkLane(petX, 1.0, petIdx);
            checkLane(sysX, 2.0, sysIdx);
        }
        ImPlot::EndPlot();
    }
    ImPlot::PopStyleVar();
    if (hovered) ImGui::SetTooltip("%.3fs / %s / %s\nDamage %.1f / Mana %.0f", (*hovered)["time"].get<double>(), (*hovered)["event"].get_ref<const std::string&>().c_str(), (*hovered)["spell"].get_ref<const std::string&>().c_str(), (*hovered)["damage"].get<double>(), (*hovered)["mana"].get<double>());
    ImGui::TextDisabled("%zu matching events", visible_.size());
    if (ImGui::BeginTable("events", 8, ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerH, ImVec2(0, 340))) {
        for (const char* label : {"Time", "Actor", "Event", "Spell", "Damage", "Mana", "Pet mana", "Outcome"}) ImGui::TableSetupColumn(label);
        ImGui::TableSetupScrollFreeze(0, 1); ImGui::TableHeadersRow();
        ImGuiListClipper clipper; clipper.Begin(static_cast<int>(visible_.size()));
        while (clipper.Step()) for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const auto& event = events[visible_[i]];
            ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextDisabled("%.3f", event["time"].get<double>());
            for (const char* key : {"actor", "event", "spell"}) { ImGui::TableNextColumn(); ImGui::TextUnformatted(event[key].get_ref<const std::string&>().c_str()); }
            ImGui::TableNextColumn();
            const double dmg = event["damage"].get<double>();
            if (dmg > 0) ImGui::TextColored(ImVec4(0.98f, 0.76f, 0.40f, 1.0f), "%.1f", dmg);
            else ImGui::TextDisabled("-");
            for (const char* key : {"mana", "petMana"}) { ImGui::TableNextColumn(); ImGui::Text("%.1f", event[key].get<double>()); }
            ImGui::TableNextColumn();
            const unsigned flags = event["flags"];
            if (flags & 2) ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.45f, 1.0f), "MISS");
            else if (flags & 1) ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.35f, 1.0f), "CRIT");
        }
        ImGui::EndTable();
    }
}
void DetailedView::draw() {
    if (data_.is_null()) return;
    ImGui::SeparatorText("Detailed analysis");
    ImGui::PushFont(nullptr, 13);
    if (data_["candidates"].size() > 1 && ImGui::BeginCombo("Build##details", data_["candidates"][candidate_]["candidateId"].get_ref<const std::string&>().c_str())) {
        for (size_t i = 0; i < data_["candidates"].size(); ++i) if (ImGui::Selectable(data_["candidates"][i]["candidateId"].get_ref<const std::string&>().c_str(), candidate_ == static_cast<int>(i))) {
            candidate_ = static_cast<int>(i); from_ = 0; until_ = data_["candidates"][i]["duration"]; filter();
        }
        ImGui::EndCombo();
    }
    const auto& candidate = data_["candidates"][candidate_];
    ImGui::TextDisabled("%u fights / %.0fs / aggregate averages", candidate["fights"].get<unsigned>(), candidate["duration"].get<double>());
    if (ImGui::BeginTabBar("analysis-tabs")) {
        if (ImGui::BeginTabItem("Damage")) {
            if (ImGui::BeginTable("damage", 9, ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                for (const char* label : {"Source", "DPS", "Share", "Damage", "Casts", "Hits", "Crits", "Misses", "Ticks"}) ImGui::TableSetupColumn(label);
                ImGui::TableHeadersRow();
                for (const auto& row : candidate["damage"]) {
                    ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(row["name"].get_ref<const std::string&>().c_str());
                    for (const char* key : {"dps", "share", "damage", "casts", "hits", "crits", "misses", "ticks"}) {
                        ImGui::TableNextColumn(); const double value = row[key]; if (std::string(key) == "share") ImGui::Text("%.1f%%", value * 100); else ImGui::Text("%.1f", value);
                    }
                }
                ImGui::EndTable();
            }
            const double difference = candidate["damageReconciliation"]["difference"];
            if (std::abs(difference) > .1) ImGui::TextDisabled("Damage rounding / unassigned: %.2f per fight", difference);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Uptimes")) {
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImVec4(.40f, .65f, .92f, 1));
            if (ImGui::BeginTable("uptimes", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {
                for (const char* label : {"Effect", "Uptime", "Active seconds"}) ImGui::TableSetupColumn(label);
                ImGui::TableHeadersRow();
                for (const auto& effect : candidate["uptimes"]) {
                    ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(effect["name"].get_ref<const std::string&>().c_str());
                    ImGui::TableNextColumn(); char label[32]; std::snprintf(label, sizeof(label), "%.1f%%", effect["fraction"].get<double>() * 100); ImGui::ProgressBar(effect["fraction"].get<float>(), ImVec2(-1, 0), label);
                    ImGui::TableNextColumn(); ImGui::Text("%.2f", effect["seconds"].get<double>());
                }
                ImGui::EndTable();
            }
            ImGui::PopStyleColor(); ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Timeline")) { drawTimeline(); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    ImGui::PopFont();
}
}
