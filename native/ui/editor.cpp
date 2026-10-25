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
    // Upload all local spell textures once before any job starts.
    for (const auto& action : catalog_.at("actions")) {
        const auto icon = action.at("icon").get<std::string>(); if (icons_.count(icon)) continue;
        png_image image{}; image.version = PNG_IMAGE_VERSION;
        if (!png_image_begin_read_from_file(&image, (root_ / "assets/icons" / icon).c_str())) continue;
        image.format = PNG_FORMAT_RGBA; std::vector<unsigned char> pixels(PNG_IMAGE_SIZE(image));
        if (png_image_finish_read(&image, nullptr, pixels.data(), 0, nullptr)) {
            GLuint texture; glGenTextures(1, &texture); glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.width, image.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data()); icons_[icon] = texture;
        }
        png_image_free(&image);
    }
}
Editor::~Editor() { for (const auto& entry : icons_) glDeleteTextures(1, &entry.second); }
void Editor::load(const Json& candidate) {
    const auto text = candidate.at("apl").at("text").get<std::string>(); if (text.size() >= apl_.size()) throw std::runtime_error("APL exceeds 8192 bytes");
    candidate_ = candidate; std::fill(apl_.begin(), apl_.end(), 0); std::copy(text.begin(), text.end(), apl_.begin()); dirty_ = true; updateIcons();
}
void Editor::save() {
    candidate_["apl"] = {{"kind", "text"}, {"text", apl_.data()}};
    fs::create_directories(root_ / "artifacts/editor");
    const auto path = root_ / "artifacts/editor/draft.json";
    { std::ofstream file(path.string() + ".tmp"); file << candidate_.dump(2); if (!file) throw std::runtime_error("Cannot save draft"); }
    fs::rename(path.string() + ".tmp", path);
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
void Editor::submit(JobRequest& request) { save(); prepareCandidate(request, candidate_); dirty_ = false; message_ = "Ready"; }
void Editor::draw(bool disabled) {
    ImGui::BeginDisabled(disabled);
    if (ImGui::BeginCombo("Build", preset_ < 0 ? "Custom" : catalog_["presets"][preset_]["name"].get_ref<const std::string&>().c_str())) {
        for (size_t i = 0; i < catalog_["presets"].size(); ++i) if (ImGui::Selectable(catalog_["presets"][i]["name"].get_ref<const std::string&>().c_str(), preset_ == static_cast<int>(i))) {
            preset_ = static_cast<int>(i); load(catalog_["presets"][i]["candidate"]); message_.clear();
        }
        ImGui::EndCombo();
    }
    if (ImGui::CollapsingHeader("Import")) {
        ImGui::InputTextMultiline("##import", import_.data(), import_.size(), ImVec2(-1, 90));
        if (ImGui::Button("Paste")) { const char* text = ImGui::GetClipboardText(); if (text) { std::snprintf(import_.data(), import_.size(), "%s", text); } }
        ImGui::SameLine();
        if (ImGui::Button("Import JSON")) { try { const auto value = editorCommand(root_, "import", Json::parse(import_.data())); load(value); preset_ = -1; save(); message_ = "Imported"; } catch (const std::exception& e) { message_ = e.what(); } }
        ImGui::SameLine(); ImGui::TextDisabled("Forever / Classic JSON");
    }
    if (ImGui::BeginTable("setup", 2, ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Character", ImGuiTableColumnFlags_WidthStretch, 0.43f); ImGui::TableSetupColumn("APL", ImGuiTableColumnFlags_WidthStretch, 0.57f);
        ImGui::TableNextColumn();
        ImGui::PushFont(nullptr, 13.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7, 4));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 5));
        auto& options = candidate_["classOptions"];
        auto combo = [&](const char* label, const char* key, const std::vector<std::string>& values) {
            const std::string current = options.at(key);
            if (ImGui::BeginCombo(label, current.c_str())) { for (const auto& v : values) if (ImGui::Selectable(v.c_str(), current == v)) { options[key] = v; dirty_ = true; } ImGui::EndCombo(); }
        };
        ImGui::SeparatorText("Character"); ImGui::PushItemWidth(150);
        combo("Race", "race", {"HUMAN", "GNOME", "ORC", "TROLL", "UNDEAD"});
        combo("Pet", "pet", {"none", "imp", "succubus"}); combo("Sacrifice", "sacrifice", {"none", "imp", "succubus"});
        combo("Fallback", "rotation", {"shadow", "fire", "searing"});
        int duration = candidate_["encounter"]["duration"]; if (ImGui::InputInt("Seconds", &duration)) { candidate_["encounter"]["duration"] = std::clamp(duration, 1, 1800); dirty_ = true; }
        ImGui::PopItemWidth();
        ImGui::SeparatorText("Talents");
        if (ImGui::BeginTabBar("talentTrees")) {
            const char* trees[] = {"affliction", "demonology", "destruction"}; const char* names[] = {"Affliction", "Demonology", "Destruction"};
            for (int tree = 0; tree < 3; ++tree) if (ImGui::BeginTabItem(names[tree])) {
                int total = 0; for (const auto& entry : candidate_["talents"].items()) for (const auto& rank : entry.value().items()) total += rank.value().get<int>();
                ImGui::TextDisabled("%d / 51", total);
                ImGui::BeginChild("talents", ImVec2(0, 245), ImGuiChildFlags_None);
                if (ImGui::BeginTable("ranks", 2, ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch); ImGui::TableSetupColumn("Rank", ImGuiTableColumnFlags_WidthFixed, 108);
                    for (const auto& talent : catalog_["talents"]) if (talent["tree"] == tree) {
                        const std::string id = talent["id"]; const int maximum = talent["max"];
                        int rank = candidate_["talents"][trees[tree]].value(id, 0);
                        ImGui::PushID(id.c_str()); ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(talent["name"].get_ref<const std::string&>().c_str());
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Tier %d / requires %d points in earlier tiers", talent["row"].get<int>(), (talent["row"].get<int>() - 1) * 5);
                        ImGui::TableNextColumn();
                        ImGui::BeginDisabled(rank == 0); if (ImGui::SmallButton("-")) { --rank; dirty_ = true; } ImGui::EndDisabled(); ImGui::SameLine(); ImGui::Text("%d/%d", rank, maximum); ImGui::SameLine();
                        ImGui::BeginDisabled(rank == maximum || total >= 51); if (ImGui::SmallButton("+")) { ++rank; dirty_ = true; } ImGui::EndDisabled();
                        candidate_["talents"][trees[tree]][id] = rank; ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                ImGui::EndChild(); ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        if (ImGui::CollapsingHeader("Stats & buffs")) {
            auto label = [](const std::string& key) {
                std::string text; for (unsigned char c : key) { if (std::isupper(c)) text += ' '; text += c; }
                if (!text.empty()) text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0]))); return text;
            };
            ImGui::TextDisabled("Bonus stats");
            ImGui::PushItemWidth(120);
            for (const char* key : {"spellPower", "shadowPower", "firePower", "intellect", "stamina", "spirit", "hit", "crit", "mp5", "penetration"}) {
                float value = candidate_["stats"].value(key, 0.0f); if (ImGui::InputFloat(label(key).c_str(), &value)) { candidate_["stats"][key] = value; dirty_ = true; }
            }
            for (const auto& entry : catalog_["buffKeys"]) { const std::string key = entry; bool value = candidate_["buffs"].value(key, false); if (ImGui::Checkbox(label(key).c_str(), &value)) { candidate_["buffs"][key] = value; dirty_ = true; } }
            ImGui::PopItemWidth();
        }
        ImGui::PopStyleVar(2);
        ImGui::PopFont();
        ImGui::TableNextColumn(); ImGui::SeparatorText("Action priority");
        ImGui::TextDisabled("One action + condition per line");
        const ImVec2 origin = ImGui::GetCursorScreenPos(); const float lineHeight = ImGui::GetTextLineHeight();
        const float editorHeight = 350;
        auto* parent = ImGui::GetCurrentWindow(); const ImGuiID textID = parent->GetID("##apl");
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 24);
        if (ImGui::InputTextMultiline("##apl", apl_.data(), apl_.size(), ImVec2(-1, editorHeight), ImGuiInputTextFlags_AllowTabInput)) { dirty_ = true; updateIcons(); }
        // Pinned ImGui child scroll keeps the presentation gutter aligned with text.
        char childName[512]; std::snprintf(childName, sizeof(childName), "%s/##apl_%08X", parent->Name, textID);
        const auto* child = ImGui::FindWindowByName(childName); const float scroll = child ? child->Scroll.y : 0;
        auto* draw = ImGui::GetWindowDrawList();
        draw->PushClipRect(origin, ImVec2(origin.x + 23, origin.y + editorHeight), true);
        for (size_t i = 0; i < rows_.size(); ++i) {
            const auto found = icons_.find(rows_[i].get<std::string>());
            const float y = origin.y + ImGui::GetStyle().FramePadding.y + i * lineHeight - scroll;
            if (found != icons_.end() && y + lineHeight >= origin.y && y < origin.y + editorHeight)
                draw->AddImage(static_cast<ImTextureID>(found->second), ImVec2(origin.x + 2, y), ImVec2(origin.x + 2 + lineHeight, y + lineHeight));
        }
        draw->PopClipRect();
        if (ImGui::Button("Validate & save")) { try { save(); const auto result = editorCommand(root_, "prepare", candidate_); (void)result; dirty_ = false; message_ = "Ready"; } catch (const std::exception& e) { message_ = e.what(); } }
        ImGui::SameLine(); if (ImGui::Button("Copy build")) { try { save(); ImGui::SetClipboardText(candidate_.dump(2).c_str()); message_ = "Copied"; } catch (const std::exception& e) { message_ = e.what(); } }
        ImGui::SameLine(); if (dirty_) ImGui::TextDisabled("Draft");
        ImGui::EndTable();
    }
    if (!message_.empty()) ImGui::TextWrapped("%s", message_.c_str());
    ImGui::EndDisabled();
}
}
