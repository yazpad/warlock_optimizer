#pragma once
#include "native_job.h"
#include <map>
namespace fastsim {
// Preparation is explicit and outside native timing/dispatch. GUI and CLI share it.
nlohmann::json editorCommand(const std::filesystem::path& root, const std::string& command, const nlohmann::json& input);
void prepareCandidate(JobRequest& request, const nlohmann::json& candidate);
class Editor {
public:
    explicit Editor(const std::filesystem::path& root, const std::string& initialCandidate = "");
    ~Editor();
    void draw(bool disabled);
    void submit(JobRequest& request);
private:
    void load(const nlohmann::json& candidate);
    void save();
    void updateIcons();
    void validateLive();
    bool importFromText(const std::string& text);
    std::filesystem::path root_;
    nlohmann::json catalog_, candidate_, rows_;
    std::vector<char> import_, apl_;
    std::string message_;
    std::string importFeedback_;
    bool importFeedbackSuccess_ = false;
    std::string validationError_;
    std::string importedLink_;
    std::string importedSourceTitle_;
    int errorLine_ = -1;
    bool dirty_ = true;
    int preset_ = 0;
    int statMode_ = 0; // 0 = Bare Stats (Manual / Direct), 1 = Equipped Gear
    std::map<std::string, unsigned int> icons_;
public:
    bool handleDropFile(const char* path);
    bool handleDropText(const char* text);
};
}
