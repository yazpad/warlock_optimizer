# Dear ImGui snapshot

Upstream: https://github.com/ocornut/imgui
Commit: `f5f6ca07be7ce0ea9eed6c04d55833bac3f6b50b`
Header version: `1.92.7` (docking branch snapshot; not asserted to be a release tag).
License: MIT; preserved in `LICENSE.txt`.

Unmodified core files and SDL3/OpenGL3 backends copied from a clean local upstream checkout. Only required files are vendored; no download or git submodule is needed to build. `SHA256SUMS` pins their contents. OpenGL renders only the frontend; simulation stays in the existing independent native Vulkan engine. Updating this snapshot requires an explicit dependency update and rebuilding/testing the frontend.
