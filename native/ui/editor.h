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
    std::filesystem::path root_;
    nlohmann::json catalog_, candidate_, rows_;
    std::vector<char> import_, apl_;
    std::string message_;
    bool dirty_ = true;
    int preset_ = 0;
    std::map<std::string, unsigned int> icons_;
};
}
