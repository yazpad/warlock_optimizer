#include "editor.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <cctype>
#include <cerrno>
#include <SDL3/SDL_opengl.h>
#include <png.h>
#include <algorithm>
#include <fstream>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
namespace fs = std::filesystem;
using Json = nlohmann::json;
namespace fastsim {
Json editorCommand(const fs::path& root, const std::string& command, const Json& input) {
    const auto parent = root / "artifacts/editor"; fs::create_directories(parent);
    std::string pattern = (parent / "prepare-XXXXXX").string();
    std::vector<char> buffer(pattern.begin(), pattern.end()); buffer.push_back(0);
    char* name = mkdtemp(buffer.data()); if (!name) throw std::runtime_error("Cannot create editor workspace");
    const fs::path directory(name), source = directory / "input.json", output = directory / "output.json";
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ignored; fs::remove_all(path, ignored); } } cleanup{directory};
    { std::ofstream file(source); file << input.dump(); if (!file) throw std::runtime_error("Cannot save editor input"); }
    std::vector<std::string> args = {"node", (root / "tools/editor_config.mjs").string(), command, source.string(), output.string()};
    std::vector<char*> argv; for (auto& arg : args) argv.push_back(arg.data()); argv.push_back(nullptr);
    pid_t child; if (posix_spawnp(&child, "node", nullptr, nullptr, argv.data(), environ) != 0) throw std::runtime_error("Cannot start configuration compiler (requires Node.js)");
    int status = 0; while (waitpid(child, &status, 0) < 0) { if (errno != EINTR) throw std::runtime_error("Cannot wait for configuration compiler"); }
    std::ifstream file(output); if (!file) throw std::runtime_error("Configuration compiler failed");
    const Json result = Json::parse(file);
    if (!result.value("ok", false)) throw std::runtime_error(result.value("error", "Invalid configuration"));
    if (!WIFEXITED(status) || WEXITSTATUS(status)) throw std::runtime_error("Configuration compiler failed");
    return result.at("value");
}
void prepareCandidate(JobRequest& request, const Json& candidate) {
    const auto result = editorCommand(request.root, "prepare", candidate);
    request.suiteSource = result.at("suiteSource").get<std::string>(); request.suiteHash = result.at("suiteHash").get<std::string>();
    request.candidateSource = candidate.dump(2);
}
Editor::Editor(const fs::path& root, const std::string& initialCandidate) : root_(root), import_(262145, 0), apl_(8193, 0) {
    catalog_ = editorCommand(root, "catalog", nullptr);
    load(catalog_.at("presets")[0].at("candidate"));
    std::ifstream saved(root_ / "artifacts/editor/draft.json");
    if (!initialCandidate.empty()) { load(Json::parse(initialCandidate)); preset_ = -1; }
    else if (saved) { try { load(Json::parse(saved)); preset_ = -1; message_ = "Draft restored"; } catch (const std::exception& e) { message_ = e.what(); } }
    // Upload all local spell and option textures once before any job starts.
    auto loadTexture = [&](const std::string& icon) {
        if (icon.empty() || icons_.count(icon)) return;
        png_image image{}; image.version = PNG_IMAGE_VERSION;
        if (!png_image_begin_read_from_file(&image, (root_ / "assets/icons" / icon).c_str())) return;
        image.format = PNG_FORMAT_RGBA; std::vector<unsigned char> pixels(PNG_IMAGE_SIZE(image));
        if (png_image_finish_read(&image, nullptr, pixels.data(), 0, nullptr)) {
            GLuint texture; glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data()); icons_[icon] = texture;
        }
        png_image_free(&image);
    };

    for (const auto& action : catalog_.at("actions")) {
        loadTexture(action.at("icon").get<std::string>());
    }
    // Also load option icons
    const std::vector<std::string> extraIcons = {
        "race_human.png", "race_gnome.png", "race_orc.png", "race_troll.png", "race_undead.png",
        "Spell_Shadow_SummonImp.png", "Spell_Shadow_SummonSuccubus.png", "icon_none.png",
        "Spell_Shadow_ShadowBolt.png", "Spell_Fire_Fireball.png", "Spell_Fire_Immolation.png"
    };
    for (const auto& icon : extraIcons) {
        loadTexture(icon);
    }
}
Editor::~Editor() { for (const auto& entry : icons_) glDeleteTextures(1, &entry.second); }
bool Editor::importFromText(const std::string& text) {
    if (text.empty()) return false;
    try {
        const auto first = text.find_first_not_of(" \t\r\n");
        if (first != std::string::npos && (text.compare(first, 7, "http://") == 0 || text.compare(first, 8, "https://") == 0)) {
            throw std::runtime_error("This looks like a WoWSims profile link. Import a JSON export; compressed profile links are unsupported.");
        }
        const auto parsed = Json::parse(text);
        const auto value = editorCommand(root_, "import", parsed);
        load(value);
        preset_ = -1;
        save();
        message_ = "Imported";

        // Extract a human-readable title or URL if present
        std::string sourceName;
        if (parsed.contains("player") && parsed["player"].is_object()) {
            if (parsed["player"].contains("name") && parsed["player"]["name"].is_string()) {
                sourceName = parsed["player"]["name"].get<std::string>();
            }
        }
        if (sourceName.empty() && parsed.contains("candidateId") && parsed["candidateId"].is_string()) {
            sourceName = parsed["candidateId"].get<std::string>();
        }
        if (sourceName.empty()) sourceName = "Custom WoWSims Profile";
        importedSourceTitle_ = sourceName;
        importedLink_ = "https://wowsims.github.io/classic/";
        return true;
    } catch (const std::exception& e) {
        message_ = e.what();
        return false;
    }
}
bool Editor::handleDropFile(const char* path) {
    importFeedbackSuccess_ = false;
    if (!path || !path[0]) { importFeedback_ = "No file path received from the drop."; return false; }
    try {
        std::ifstream file(path);
        if (!file) { importFeedback_ = "Could not open the dropped file."; return false; }
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        importFeedbackSuccess_ = importFromText(content);
        importFeedback_ = importFeedbackSuccess_ ? "Imported dropped JSON export." : message_;
        return importFeedbackSuccess_;
    } catch (const std::exception& e) {
        importFeedback_ = e.what();
        return false;
    }
}
bool Editor::handleDropText(const char* text) {
    importFeedbackSuccess_ = false;
    if (!text || !text[0]) { importFeedback_ = "No text received from the drop."; return false; }
    importFeedbackSuccess_ = importFromText(text);
    importFeedback_ = importFeedbackSuccess_ ? "Imported dropped JSON export." : message_;
    return importFeedbackSuccess_;
}
void Editor::load(const Json& candidate) {
    const auto text = candidate.at("apl").at("text").get<std::string>(); if (text.size() >= apl_.size()) throw std::runtime_error("APL exceeds 8192 bytes");
    candidate_ = candidate; std::fill(apl_.begin(), apl_.end(), 0); std::copy(text.begin(), text.end(), apl_.begin()); dirty_ = true; updateIcons(); validateLive();
}
void Editor::save() {
    candidate_["apl"] = {{"kind", "text"}, {"text", apl_.data()}};
    fs::create_directories(root_ / "artifacts/editor");
    const auto path = root_ / "artifacts/editor/draft.json";
    { std::ofstream file(path.string() + ".tmp"); file << candidate_.dump(2); if (!file) throw std::runtime_error("Cannot save draft"); }
    fs::rename(path.string() + ".tmp", path);
}
void Editor::validateLive() {
    save();
    try {
        const auto result = editorCommand(root_, "prepare", candidate_);
        (void)result;
        validationError_.clear();
        errorLine_ = -1;
        message_ = "Valid";
        dirty_ = false;
    } catch (const std::exception& e) {
        validationError_ = e.what();
        errorLine_ = -1;
        // Parse line number if message starts with "Line <N>"
        const std::string prefix = "Line ";
        if (validationError_.rfind(prefix, 0) == 0) {
            size_t idx = prefix.size();
            int lineNum = 0;
            while (idx < validationError_.size() && std::isdigit(static_cast<unsigned char>(validationError_[idx]))) {
                lineNum = lineNum * 10 + (validationError_[idx] - '0');
                ++idx;
            }
            if (lineNum > 0) errorLine_ = lineNum - 1; // 0-based
        }
        message_ = validationError_;
    }
}
void Editor::updateIcons() {
    // Presentation only; executable parsing/validation belongs to editor_config.mjs.
    rows_ = Json::array(); std::string text(apl_.data()); size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find('\n', start); const std::string line = text.substr(start, end - start);
        std::string icon;
        auto compact = [](std::string s) { s.erase(std::remove_if(s.begin(), s.end(), [](unsigned char c) { return c == '_' || c == ' ' || c == '-'; }), s.end()); std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); }); return s; };
        std::string lower = line; std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
        size_t gate = lower.find(" if "); if (gate == std::string::npos) gate = lower.find(" when ");
        const auto action = compact(line.substr(0, std::min(gate, line.find('#'))));
        for (const auto& entry : catalog_.at("actions")) if (action == compact(entry.at("id").get<std::string>()) || action == compact(entry.at("spell").get<std::string>())) { icon = entry.at("icon"); break; }
        rows_.push_back(icon); if (end == std::string::npos) break; start = end + 1;
    }
}
static void drawDashedRect(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 col, float dashLen = 6.0f, float gapLen = 4.0f, float thickness = 1.0f) {
    auto drawSegment = [&](ImVec2 p1, ImVec2 p2) {
        float dx = p2.x - p1.x, dy = p2.y - p1.y;
        float dist = std::hypot(dx, dy);
        if (dist <= 0.001f) return;
        float nx = dx / dist, ny = dy / dist;
        float cur = 0.0f;
        while (cur < dist) {
            float dStart = cur;
            float dEnd = std::min(cur + dashLen, dist);
            draw->AddLine(ImVec2(p1.x + nx * dStart, p1.y + ny * dStart),
                          ImVec2(p1.x + nx * dEnd, p1.y + ny * dEnd), col, thickness);
            cur += dashLen + gapLen;
        }
    };
    drawSegment(min, ImVec2(max.x, min.y));
    drawSegment(ImVec2(max.x, min.y), max);
    drawSegment(max, ImVec2(min.x, max.y));
    drawSegment(ImVec2(min.x, max.y), min);
}

void Editor::submit(JobRequest& request) { save(); prepareCandidate(request, candidate_); dirty_ = false; message_ = "Ready"; }
void Editor::draw(bool disabled) {
    ImGui::BeginDisabled(disabled);

    // Header: Preset selection & WoWSims drag & drop import box
    const float availWidth = ImGui::GetContentRegionAvail().x;
    ImGui::TextDisabled("Configuration Preset");
    ImGui::SetNextItemWidth(std::min(availWidth, 380.0f));
    if (ImGui::BeginCombo("##BuildPreset", preset_ < 0 ? "Custom Profile" : catalog_["presets"][preset_]["name"].get_ref<const std::string&>().c_str())) {
        for (size_t i = 0; i < catalog_["presets"].size(); ++i) {
            if (ImGui::Selectable(catalog_["presets"][i]["name"].get_ref<const std::string&>().c_str(), preset_ == static_cast<int>(i))) {
                preset_ = static_cast<int>(i);
                load(catalog_["presets"][i]["candidate"]);
                message_.clear();
                importedLink_.clear();
                importedSourceTitle_.clear();
                importFeedback_.clear();
            }
        }
        ImGui::EndCombo();
    }
    if (!importedLink_.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.50f, 0.85f, 0.65f, 1.0f), "Using wowsims configuration:");
        ImGui::SameLine();
        const std::string label = importedSourceTitle_.empty() ? "Profile Link" : importedSourceTitle_;
        ImGui::TextLinkOpenURL(label.c_str(), importedLink_.c_str());
    }

    ImGui::Spacing();

    // Drop Box / Import Target
    const float dropBoxHeight = 50.0f;
    // Leave a small inset at the right edge so the import target stays inside
    // the left pane when its vertical scrollbar appears during layout.
    const float dropBoxWidth = std::max(0.0f, availWidth - 8.0f);
    const ImVec2 dropMin = ImGui::GetCursorScreenPos();
    const ImVec2 dropMax = ImVec2(dropMin.x + dropBoxWidth, dropMin.y + dropBoxHeight);

    ImGui::InvisibleButton("##import_drop_zone", ImVec2(dropBoxWidth, dropBoxHeight));
    const bool isDropHovered = ImGui::IsItemHovered();
    const bool isDropClicked = ImGui::IsItemClicked();

    if (isDropClicked) {
        // Clicking clipboard import
        const char* clip = ImGui::GetClipboardText();
        if (clip && clip[0]) {
            if (importFromText(clip)) {
                message_ = "Imported from clipboard";
            }
        } else {
            message_ = "Clipboard is empty";
        }
    }

    auto* fgDraw = ImGui::GetWindowDrawList();
    const ImU32 bgCol = isDropHovered ? IM_COL32(35, 42, 58, 220) : IM_COL32(18, 22, 30, 160);
    const ImU32 borderCol = isDropHovered ? IM_COL32(150, 180, 255, 255) : IM_COL32(85, 100, 130, 200);

    fgDraw->AddRectFilled(dropMin, dropMax, bgCol, 6.0f);
    drawDashedRect(fgDraw, dropMin, dropMax, borderCol, 6.0f, 4.0f, isDropHovered ? 1.5f : 1.0f);

    const char* dropText1 = "Drop a WoWSims JSON export or click to paste JSON";
    const char* dropText2 = "(Forever / Classic apiVersion 2 JSON export)";
    const ImVec2 size1 = ImGui::CalcTextSize(dropText1);
    const ImVec2 size2 = ImGui::CalcTextSize(dropText2);

    const float text1X = dropMin.x + (dropBoxWidth - size1.x) * 0.5f;
    const float text1Y = dropMin.y + 7.0f;
    const float text2X = dropMin.x + (dropBoxWidth - size2.x) * 0.5f;
    const float text2Y = dropMin.y + 27.0f;

    fgDraw->AddText(ImVec2(text1X, text1Y), isDropHovered ? IM_COL32(230, 240, 255, 255) : IM_COL32(180, 195, 220, 255), dropText1);
    fgDraw->AddText(ImVec2(text2X, text2Y), IM_COL32(120, 135, 160, 220), dropText2);

    if (!importFeedback_.empty()) {
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, importFeedbackSuccess_ ? ImVec4(0.45f, 0.85f, 0.58f, 1.0f) : ImVec4(0.95f, 0.55f, 0.42f, 1.0f));
        ImGui::TextWrapped("%s", importFeedback_.c_str());
        ImGui::PopStyleColor();
    }
    if (candidate_.contains("importWarnings") && candidate_["importWarnings"].is_array() && !candidate_["importWarnings"].empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Import warnings");
        for (const auto& warning : candidate_["importWarnings"]) {
            if (!warning.is_string()) continue;
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.78f, 0.30f, 1.0f));
            ImGui::BulletText("%s", warning.get_ref<const std::string&>().c_str());
            ImGui::PopStyleColor();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::PushFont(nullptr, 13.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 5));
    auto& options = candidate_["classOptions"];

    // Helper for rendering an icon button with a popup selection menu (like in classic sim UI)
    struct IconOption {
        std::string value;
        std::string tooltip;
        std::string icon;
    };
    auto iconButtonSelect = [&](const char* id, const char* label, const char* key, const std::vector<IconOption>& items) {
        const std::string current = options.at(key);
        const IconOption* curItem = nullptr;
        for (const auto& item : items) {
            if (item.value == current) { curItem = &item; break; }
        }
        if (!curItem && !items.empty()) curItem = &items[0];

        const float iconSize = 36.0f;
        auto getTex = [&](const std::string& iconFile) -> ImTextureID {
            auto it = icons_.find(iconFile);
            return (it != icons_.end()) ? static_cast<ImTextureID>(it->second) : 0;
        };

        ImGui::BeginGroup();
        // Top label like "RACE", "PET", "DS"
        ImGui::TextColored(ImVec4(0.45f, 0.65f, 0.90f, 1.0f), "%s", label);

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.08f, 0.10f, 0.14f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.20f, 0.25f, 0.35f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.30f, 0.38f, 0.50f, 1.0f));

        const bool isNone = curItem && curItem->value == "none";
        ImTextureID curTex = curItem && !isNone ? getTex(curItem->icon) : 0;
        std::string btnId = std::string("##btn_") + id;
        const bool clicked = isNone
            ? ImGui::Button(btnId.c_str(), ImVec2(iconSize + 4.0f, iconSize + 4.0f))
            : ImGui::ImageButton(btnId.c_str(), curTex, ImVec2(iconSize, iconSize));
        if (clicked) {
            ImGui::OpenPopup(id);
        }

        // Draw golden/bronze icon border like in WoW
        ImDrawList* draw = ImGui::GetWindowDrawList();
        ImVec2 pMin = ImGui::GetItemRectMin();
        ImVec2 pMax = ImGui::GetItemRectMax();
        if (isNone) {
            draw->AddRectFilled(ImVec2(pMin.x + 5.0f, pMin.y + 5.0f), ImVec2(pMax.x - 5.0f, pMax.y - 5.0f), IM_COL32(42, 46, 54, 255), 2.0f);
            draw->AddRect(ImVec2(pMin.x + 5.0f, pMin.y + 5.0f), ImVec2(pMax.x - 5.0f, pMax.y - 5.0f), IM_COL32(75, 82, 95, 210), 2.0f, 0, 1.0f);
        }
        draw->AddRect(pMin, pMax, IM_COL32(180, 150, 70, 220), 3.0f, 0, 1.5f);

        if (ImGui::IsItemHovered() && curItem) {
            ImGui::SetTooltip("%s: %s (click to change)", label, curItem->tooltip.c_str());
        }

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();

        // Popup selection
        if (ImGui::BeginPopup(id)) {
            ImGui::TextDisabled("Select %s", label);
            ImGui::Separator();
            for (const auto& item : items) {
                ImGui::PushID(item.value.c_str());
                const bool isSelected = (current == item.value);
                ImTextureID tex = getTex(item.icon);
                if (ImGui::Selectable("##select_item", isSelected, ImGuiSelectableFlags_None, ImVec2(140, 26))) {
                    options[key] = item.value;
                    dirty_ = true;
                }
                ImGui::SameLine(6.0f);
                if (item.value == "none") {
                    const ImVec2 squareMin = ImGui::GetCursorScreenPos();
                    ImGui::Dummy(ImVec2(22, 22));
                    ImGui::GetWindowDrawList()->AddRectFilled(squareMin, ImVec2(squareMin.x + 22, squareMin.y + 22), IM_COL32(42, 46, 54, 255), 2.0f);
                    ImGui::GetWindowDrawList()->AddRect(squareMin, ImVec2(squareMin.x + 22, squareMin.y + 22), IM_COL32(75, 82, 95, 210), 2.0f);
                    ImGui::SameLine();
                } else if (tex) {
                    ImGui::Image(tex, ImVec2(22, 22));
                    ImGui::SameLine();
                }
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(item.tooltip.c_str());
                ImGui::PopID();
            }
            ImGui::EndPopup();
        }
        ImGui::EndGroup();
    };

    ImGui::SeparatorText("Character & Encounter");

    // Clean horizontal group of square icon selectors with a neat border card
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.09f, 0.12f, 0.50f));
    ImGui::BeginChild("char_card", ImVec2(0, 72), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar);
    ImGui::SetCursorPos(ImVec2(12, 8));

    // Race
    const std::vector<IconOption> raceOptions = {
        {"HUMAN", "Human", "race_human.png"},
        {"GNOME", "Gnome", "race_gnome.png"},
        {"ORC", "Orc", "race_orc.png"},
        {"TROLL", "Troll", "race_troll.png"},
        {"UNDEAD", "Undead", "race_undead.png"}
    };
    iconButtonSelect("popup_race", "RACE", "race", raceOptions);

    ImGui::SameLine(0, 16);
    // Pet
    const std::vector<IconOption> petOptions = {
        {"none", "None", "icon_none.png"},
        {"imp", "Imp", "Spell_Shadow_SummonImp.png"},
        {"succubus", "Succubus", "Spell_Shadow_SummonSuccubus.png"}
    };
    iconButtonSelect("popup_pet", "PET", "pet", petOptions);

    ImGui::SameLine(0, 16);
    // Sacrifice (DS)
    const std::vector<IconOption> sacrificeOptions = {
        {"none", "None", "icon_none.png"},
        {"imp", "Imp", "Spell_Shadow_SummonImp.png"},
        {"succubus", "Succubus", "Spell_Shadow_SummonSuccubus.png"}
    };
    iconButtonSelect("popup_ds", "DS", "sacrifice", sacrificeOptions);

    ImGui::SameLine(0, 20);
    // Vertical divider
    ImDrawList* cardDraw = ImGui::GetWindowDrawList();
    float divX = ImGui::GetCursorScreenPos().x + 2.0f;
    float divYMin = ImGui::GetCursorScreenPos().y + 4.0f;
    float divYMax = divYMin + 44.0f;
    cardDraw->AddLine(ImVec2(divX, divYMin), ImVec2(divX, divYMax), IM_COL32(70, 75, 95, 120), 1.0f);
    ImGui::Dummy(ImVec2(8, 0));
    ImGui::SameLine(0, 14);

    // Encounter duration inline inside the card
    ImGui::BeginGroup();
    ImGui::TextColored(ImVec4(0.45f, 0.65f, 0.90f, 1.0f), "DURATION");
    int duration = candidate_["encounter"]["duration"];
    ImGui::SetNextItemWidth(90);
    if (ImGui::InputInt("##EncounterDuration", &duration, 10, 60)) {
        candidate_["encounter"]["duration"] = std::clamp(duration, 1, 1800);
        dirty_ = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("s (%dm %ds)", duration / 60, duration % 60);
    ImGui::EndGroup();

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::Spacing();

    if (ImGui::CollapsingHeader("Talents")) {
        const char* treeKeys[] = {"affliction", "demonology", "destruction"};
        const char* treeNames[] = {"Affliction", "Demonology", "Destruction"};

        int treePoints[3] = {0, 0, 0};
        for (int t = 0; t < 3; ++t) {
            for (const auto& rank : candidate_["talents"][treeKeys[t]].items()) {
                treePoints[t] += rank.value().get<int>();
            }
        }
        int totalPoints = treePoints[0] + treePoints[1] + treePoints[2];

        // Overall talent header bar showing points in all 3 trees and total out of 51
        ImGui::TextColored(ImVec4(0.70f, 0.75f, 0.85f, 1.0f), "Aff: %d | Demo: %d | Dest: %d", treePoints[0], treePoints[1], treePoints[2]);
        ImGui::SameLine();
        ImGui::TextDisabled("| Total: %d / 51", totalPoints);
        ImGui::SameLine();
        ImGui::TextDisabled("(Left click adds, right click removes)");
        ImGui::ProgressBar(std::min(1.0f, static_cast<float>(totalPoints) / 51.0f), ImVec2(-1, 4), "");

        // Pre-organize talents by [tree][row]
        std::vector<nlohmann::json> grid[3][8];
        for (const auto& talent : catalog_["talents"]) {
            int t = talent["tree"].get<int>();
            int r = talent["row"].get<int>();
            if (t >= 0 && t < 3 && r >= 1 && r <= 7) {
                grid[t][r].push_back(talent);
            }
        }

        // Talent table with 3 equal columns, showing Affliction, Demonology, Destruction simultaneously
        if (ImGui::BeginTable("talents_three_trees", 3, ImGuiTableFlags_None)) {
            ImGui::TableSetupColumn("Affliction", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Demonology", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Destruction", ImGuiTableColumnFlags_WidthStretch);

            // Tree header row with tree names and point counts
            ImGui::TableNextRow();
            for (int t = 0; t < 3; ++t) {
                ImGui::TableNextColumn();
                ImGui::TextColored(ImVec4(0.55f, 0.78f, 1.0f, 1.0f), "%s (%d pts)", treeNames[t], treePoints[t]);
            }

            // Loop through tiers 1 to 7
            for (int r = 1; r <= 7; ++r) {
                // Tier separator line across all 3 columns
                ImGui::TableNextRow();
                for (int t = 0; t < 3; ++t) {
                    ImGui::TableNextColumn();
                    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.35f, 0.40f, 0.52f, 0.80f));
                    ImGui::Separator();
                    ImGui::PopStyleColor();
                }

                // Determine max number of talents in this tier across the 3 trees
                int maxInTier = std::max({(int)grid[0][r].size(), (int)grid[1][r].size(), (int)grid[2][r].size()});

                // Render slots within this tier
                for (int slot = 0; slot < maxInTier; ++slot) {
                    ImGui::TableNextRow();
                    for (int t = 0; t < 3; ++t) {
                        ImGui::TableNextColumn();
                        if (slot < (int)grid[t][r].size()) {
                            const auto& talent = grid[t][r][slot];
                            const std::string id = talent["id"];
                            const int maximum = talent["max"];
                            const int requiredPoints = (r - 1) * 5;
                            const bool tierUnlocked = (treePoints[t] >= requiredPoints);
                            int rank = candidate_["talents"][treeKeys[t]].value(id, 0);

                            ImGui::PushID(id.c_str());
                            ImGui::AlignTextToFramePadding();

                            // Clickable talent item: Left click adds, Right click removes
                            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.20f, 0.25f, 0.35f, 0.50f));
                            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.28f, 0.35f, 0.48f, 0.70f));
                            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.35f, 0.45f, 0.60f, 0.90f));

                            char itemLabel[128];
                            std::snprintf(itemLabel, sizeof(itemLabel), "##talent_%s", id.c_str());
                            const bool canAdd = (rank < maximum && totalPoints < 51 && tierUnlocked);
                            const bool canRemove = (rank > 0 && tierUnlocked);

                            ImVec2 itemSize(0, ImGui::GetTextLineHeightWithSpacing());
                            bool clickedLeft = false;
                            if (ImGui::Selectable(itemLabel, rank > 0, ImGuiSelectableFlags_AllowOverlap, itemSize)) {
                                clickedLeft = true;
                            }
                            const bool isHovered = ImGui::IsItemHovered();
                            const bool clickedRight = isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right);

                            if (clickedLeft && canAdd) {
                                ++rank;
                                dirty_ = true;
                            } else if (clickedRight && canRemove) {
                                --rank;
                                dirty_ = true;
                            }

                            // Render text and rank over the selectable
                            ImGui::SameLine(6.0f);
                            if (rank == maximum) {
                                ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.40f, 1.0f), "%s", talent["name"].get_ref<const std::string&>().c_str());
                            } else if (rank > 0) {
                                ImGui::TextColored(ImVec4(0.85f, 0.88f, 0.95f, 1.0f), "%s", talent["name"].get_ref<const std::string&>().c_str());
                            } else {
                                ImGui::TextDisabled("%s", talent["name"].get_ref<const std::string&>().c_str());
                            }

                            ImGui::SameLine();
                            if (rank == maximum) ImGui::TextColored(ImVec4(1.0f, 0.82f, 0.40f, 1.0f), "%d/%d", rank, maximum);
                            else if (rank > 0) ImGui::TextColored(ImVec4(0.70f, 0.85f, 1.0f, 1.0f), "%d/%d", rank, maximum);
                            else ImGui::TextDisabled("%d/%d", rank, maximum);

                            if (isHovered) {
                                ImGui::SetTooltip("%s (Rank %d/%d)\nRequires %d pts in %s\nLeft-click to add rank\nRight-click to remove rank",
                                    talent["name"].get_ref<const std::string&>().c_str(), rank, maximum, requiredPoints, treeNames[t]);
                            }

                            ImGui::PopStyleColor(3);

                            candidate_["talents"][treeKeys[t]][id] = rank;
                            ImGui::PopID();
                        }
                    }
                }
            }
            ImGui::EndTable();
        }
    }
    if (ImGui::CollapsingHeader("Stats & buffs")) {
        auto label = [](const std::string& key) {
            std::string text; for (unsigned char c : key) { if (std::isupper(c)) text += ' '; text += c; }
            if (!text.empty()) text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0]))); return text;
        };

        // Toggle between Bare Stats and Equipped Gear
        ImGui::TextDisabled("Stat Input Mode");
        ImGui::RadioButton("Bare / Direct Stats", &statMode_, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Equipped Gear", &statMode_, 1);
        ImGui::Spacing();

        if (statMode_ == 0) {
            ImGui::TextDisabled("Direct Character Bonus Stats");
            if (ImGui::BeginTable("bonus_stats_grid", 2, ImGuiTableFlags_None)) {
                ImGui::TableSetupColumn("s1", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("s2", ImGuiTableColumnFlags_WidthStretch);
                const char* keys[] = {"spellPower", "shadowPower", "firePower", "intellect", "stamina", "spirit", "hit", "crit", "mp5", "penetration"};
                for (int i = 0; i < 10; ++i) {
                    ImGui::TableNextColumn();
                    float value = candidate_["stats"].value(keys[i], 0.0f);
                    ImGui::PushItemWidth(100);
                    if (ImGui::InputFloat(label(keys[i]).c_str(), &value)) {
                        candidate_["stats"][keys[i]] = value;
                        dirty_ = true;
                    }
                    ImGui::PopItemWidth();
                }
                ImGui::EndTable();
            }
        } else {
            ImGui::TextDisabled("Equipped Gear & Item Stats");
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.10f, 0.13f, 0.60f));
            ImGui::BeginChild("gear_preview_box", ImVec2(0, 140), ImGuiChildFlags_Borders);
            ImGui::TextColored(ImVec4(0.50f, 0.85f, 0.65f, 1.0f), "Item & Equipment Resolution");
            ImGui::TextWrapped("Gear items are resolved via the WoW classic database (5,900+ items).");
            ImGui::TextDisabled("Current gear mode: %s", candidate_.value("equipment", Json::object()).value("mode", "direct").c_str());
            if (candidate_.contains("equipment") && candidate_["equipment"].contains("items") && !candidate_["equipment"]["items"].empty()) {
                ImGui::Text("Equipped items: %zu registered slots", candidate_["equipment"]["items"].size());
            } else {
                ImGui::TextDisabled("No individual gear items equipped in active candidate.");
            }
            ImGui::Spacing();
            if (ImGui::Button("Sync Gear -> Bare Stats")) {
                dirty_ = true;
                validateLive();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(Recalculates attributes from equipped gear database)");
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }
        ImGui::Separator();
        ImGui::TextDisabled("Raid & Consumable Buffs");
        if (ImGui::BeginTable("buffs_grid", 2, ImGuiTableFlags_None)) {
            ImGui::TableSetupColumn("b1", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("b2", ImGuiTableColumnFlags_WidthStretch);
            for (const auto& entry : catalog_["buffKeys"]) {
                ImGui::TableNextColumn();
                const std::string key = entry;
                bool value = candidate_["buffs"].value(key, false);
                if (ImGui::Checkbox(label(key).c_str(), &value)) {
                    candidate_["buffs"][key] = value;
                    dirty_ = true;
                }
            }
            ImGui::EndTable();
        }
    }
    ImGui::PopStyleVar(2);
    ImGui::PopFont();

    ImGui::PushFont(nullptr, 13.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7, 4));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 5));

    if (ImGui::CollapsingHeader("Action Priority List (APL)")) {
        // Fallback rotation text selector inline above editor
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Fallback filler rotation:");
        ImGui::SameLine();
        const std::string curRot = options.value("rotation", "shadow");
        const char* rotLabels[] = {"Shadow Bolt (shadow)", "Fireball (fire)", "Searing Pain / Immolate (searing)"};
        const char* rotValues[] = {"shadow", "fire", "searing"};
        int rotIdx = 0;
        for (int i = 0; i < 3; ++i) if (curRot == rotValues[i]) { rotIdx = i; break; }
        ImGui::SetNextItemWidth(220);
        if (ImGui::Combo("##FallbackRotation", &rotIdx, rotLabels, IM_ARRAYSIZE(rotLabels))) {
            options["rotation"] = rotValues[rotIdx];
            dirty_ = true;
        }
        ImGui::Spacing();

        ImGui::TextDisabled("One action + condition per line");
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float editorHeight = 350;
        auto* parent = ImGui::GetCurrentWindow(); const ImGuiID textID = parent->GetID("##apl");
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 24);

        ImGuiIO& io = ImGui::GetIO();
        ImFont* monoFont = io.Fonts->Fonts.size() > 1 ? io.Fonts->Fonts[1] : nullptr;
        if (monoFont) ImGui::PushFont(monoFont, 15.0f);
        const float lineHeight = ImGui::GetTextLineHeight();

        // Make input text background darker for code contrast and text transparent so syntax overlay shines through
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.07f, 0.08f, 0.10f, 0.95f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
        if (ImGui::InputTextMultiline("##apl", apl_.data(), apl_.size(), ImVec2(-1, editorHeight), ImGuiInputTextFlags_AllowTabInput)) {
            dirty_ = true;
            updateIcons();
            validateLive();
        }
        ImGui::PopStyleColor(2);

        // Pinned ImGui child scroll keeps the presentation gutter aligned with text.
        char childName[512]; std::snprintf(childName, sizeof(childName), "%s/##apl_%08X", parent->Name, textID);
        const auto* child = ImGui::FindWindowByName(childName);
        const float scrollY = child ? child->Scroll.y : 0;
        const float scrollX = child ? child->Scroll.x : 0;

        auto* draw = child ? child->DrawList : ImGui::GetWindowDrawList();

        // 1. Render syntax-highlighted text inside child window so coordinates and clipping match InputText exactly
        const float textStartX = (child ? child->Pos.x : origin.x + 24) + ImGui::GetStyle().FramePadding.x - scrollX;
        const float textStartY = (child ? child->Pos.y : origin.y) + ImGui::GetStyle().FramePadding.y - scrollY;

        std::string textStr(apl_.data());
        size_t lineIdx = 0, lineStart = 0;
        while (lineStart <= textStr.size()) {
            size_t lineEnd = textStr.find('\n', lineStart);
            if (lineEnd == std::string::npos) lineEnd = textStr.size();
            const float linePosY = textStartY + lineIdx * lineHeight;

            // Only draw lines visible within the viewport
            if (linePosY + lineHeight >= origin.y && linePosY <= origin.y + editorHeight) {
                float curX = textStartX;
                size_t p = lineStart;
                bool isAction = true;

                while (p < lineEnd) {
                    char c = textStr[p];
                    if (c == '#') {
                        // Comment
                        std::string comment = textStr.substr(p, lineEnd - p);
                        draw->AddText(monoFont, 15.0f, ImVec2(curX, linePosY), IM_COL32(110, 125, 145, 255), comment.c_str());
                        curX += monoFont ? monoFont->CalcTextSizeA(15.0f, FLT_MAX, -1.0f, comment.c_str()).x : ImGui::CalcTextSize(comment.c_str()).x;
                        break;
                    }
                    if (std::isspace(static_cast<unsigned char>(c))) {
                        size_t spStart = p;
                        while (p < lineEnd && std::isspace(static_cast<unsigned char>(textStr[p]))) ++p;
                        std::string spaces = textStr.substr(spStart, p - spStart);
                        curX += monoFont ? monoFont->CalcTextSizeA(15.0f, FLT_MAX, -1.0f, spaces.c_str()).x : ImGui::CalcTextSize(spaces.c_str()).x;
                        continue;
                    }
                    if (c == '"') {
                        size_t strStart = p++;
                        while (p < lineEnd && textStr[p] != '"') ++p;
                        if (p < lineEnd && textStr[p] == '"') ++p;
                        std::string strLit = textStr.substr(strStart, p - strStart);
                        draw->AddText(monoFont, 15.0f, ImVec2(curX, linePosY), IM_COL32(98, 220, 184, 255), strLit.c_str());
                        curX += monoFont ? monoFont->CalcTextSizeA(15.0f, FLT_MAX, -1.0f, strLit.c_str()).x : ImGui::CalcTextSize(strLit.c_str()).x;
                        isAction = false;
                        continue;
                    }
                    if (std::isdigit(static_cast<unsigned char>(c))) {
                        size_t numStart = p;
                        while (p < lineEnd && (std::isdigit(static_cast<unsigned char>(textStr[p])) || textStr[p] == '.')) ++p;
                        std::string numStr = textStr.substr(numStart, p - numStart);
                        draw->AddText(monoFont, 15.0f, ImVec2(curX, linePosY), IM_COL32(245, 166, 85, 255), numStr.c_str());
                        curX += monoFont ? monoFont->CalcTextSizeA(15.0f, FLT_MAX, -1.0f, numStr.c_str()).x : ImGui::CalcTextSize(numStr.c_str()).x;
                        isAction = false;
                        continue;
                    }
                    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '.') {
                        size_t wordStart = p;
                        while (p < lineEnd && (std::isalnum(static_cast<unsigned char>(textStr[p])) || textStr[p] == '_' || textStr[p] == '.')) ++p;
                        std::string word = textStr.substr(wordStart, p - wordStart);
                        ImU32 col = IM_COL32(220, 225, 235, 255);
                        if (word == "if" || word == "when") {
                            col = IM_COL32(216, 120, 232, 255);
                            isAction = false;
                        } else if (word.find("target.") == 0 || word.find("buff.") == 0 || word.find("debuff.") == 0 || word == "target_ttd" || word == "dots_missing") {
                            col = IM_COL32(100, 181, 246, 255);
                            isAction = false;
                        } else if (isAction) {
                            col = IM_COL32(252, 211, 77, 255); // Action verb / spell
                            isAction = false;
                        }
                        draw->AddText(monoFont, 15.0f, ImVec2(curX, linePosY), col, word.c_str());
                        curX += monoFont ? monoFont->CalcTextSizeA(15.0f, FLT_MAX, -1.0f, word.c_str()).x : ImGui::CalcTextSize(word.c_str()).x;
                        continue;
                    }
                    // Operator / punctuation
                    size_t opStart = p++;
                    while (p < lineEnd && !std::isspace(static_cast<unsigned char>(textStr[p])) &&
                           !std::isalnum(static_cast<unsigned char>(textStr[p])) && textStr[p] != '_' &&
                           textStr[p] != '.' && textStr[p] != '#' && textStr[p] != '"') ++p;
                    std::string op = textStr.substr(opStart, p - opStart);
                    draw->AddText(monoFont, 15.0f, ImVec2(curX, linePosY), IM_COL32(144, 164, 174, 255), op.c_str());
                    curX += monoFont ? monoFont->CalcTextSizeA(15.0f, FLT_MAX, -1.0f, op.c_str()).x : ImGui::CalcTextSize(op.c_str()).x;
                    isAction = false;
                }

                // If this line has an error, draw a red squiggly wave underline
                if (errorLine_ >= 0 && static_cast<int>(lineIdx) == errorLine_) {
                    const float waveY = linePosY + lineHeight - 1.0f;
                    const float waveStartX = textStartX;
                    const float waveEndX = std::max(waveStartX + 40.0f, curX);
                    const float waveLen = 4.0f;
                    const float waveAmp = 1.8f;
                    for (float wx = waveStartX; wx < waveEndX; wx += waveLen) {
                        const float wxNext = std::min(waveEndX, wx + waveLen * 0.5f);
                        const float dy1 = (static_cast<int>((wx - waveStartX) / waveLen) % 2 == 0) ? -waveAmp : waveAmp;
                        const float dy2 = -dy1;
                        draw->AddLine(ImVec2(wx, waveY + dy1), ImVec2(wxNext, waveY + dy2), IM_COL32(245, 80, 80, 240), 1.5f);
                    }
                }
            }
            if (lineEnd >= textStr.size()) break;
            lineStart = lineEnd + 1;
            ++lineIdx;
        }

        if (monoFont) ImGui::PopFont();

        // 2. Render spell icons down the left gutter
        draw->PushClipRect(origin, ImVec2(origin.x + 23, origin.y + editorHeight), true);
        for (size_t i = 0; i < rows_.size(); ++i) {
            const auto found = icons_.find(rows_[i].get<std::string>());
            const float y = origin.y + ImGui::GetStyle().FramePadding.y + i * lineHeight - scrollY;
            if (found != icons_.end() && y + lineHeight >= origin.y && y < origin.y + editorHeight) {
                draw->AddRectFilled(ImVec2(origin.x + 2, y), ImVec2(origin.x + 2 + lineHeight, y + lineHeight), IM_COL32(18, 20, 26, 255), 3.0f);
                draw->AddImage(static_cast<ImTextureID>(found->second), ImVec2(origin.x + 2, y), ImVec2(origin.x + 2 + lineHeight, y + lineHeight));
                draw->AddRect(ImVec2(origin.x + 2, y), ImVec2(origin.x + 2 + lineHeight, y + lineHeight), IM_COL32(65, 75, 95, 180), 3.0f);
            }
        }
        draw->PopClipRect();

        if (ImGui::Button("Validate & save")) { validateLive(); }
        ImGui::SameLine(); if (ImGui::Button("Copy build")) { try { save(); ImGui::SetClipboardText(candidate_.dump(2).c_str()); message_ = "Copied"; } catch (const std::exception& e) { message_ = e.what(); } }
        ImGui::SameLine();
        if (!validationError_.empty()) {
            ImGui::TextColored(ImVec4(0.95f, 0.40f, 0.40f, 1.0f), "Invalid");
        } else if (dirty_) {
            ImGui::TextDisabled("Draft");
        } else {
            ImGui::TextColored(ImVec4(0.40f, 0.85f, 0.55f, 1.0f), "Valid");
        }

        if (!message_.empty() && message_ != "Valid" && validationError_.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.55f, 0.80f, 1.0f, 1.0f), "| %s", message_.c_str());
        }

        if (!validationError_.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.45f, 0.45f, 1.0f));
            ImGui::TextWrapped("%s", validationError_.c_str());
            ImGui::PopStyleColor();
        }
    }
    ImGui::PopStyleVar(2);
    ImGui::PopFont();
    ImGui::EndDisabled();
}
}
