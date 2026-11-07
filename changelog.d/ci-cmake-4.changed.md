**CI installs CMake 4.** The hosted runners ship CMake 3.x, which rejects the project's presets, so the workflows now install the version named in `.github/toolchain-versions.json`.
