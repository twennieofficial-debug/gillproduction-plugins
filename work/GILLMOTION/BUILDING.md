Build with CMake 3.22+, C++17 and the pinned JUCE 8.0.12 source in ../dependencies/JUCE. Keep ../GILLCommon and ../GILLNEXT/ThirdParty with this group.

cmake -S GILLMOTION -B build-motion -DCMAKE_BUILD_TYPE=Release
cmake --build build-motion --config Release --parallel 2
ctest --test-dir build-motion -C Release --output-on-failure

Update11 delivery is Windows x64 only. Mac is deferred pending user approval.
