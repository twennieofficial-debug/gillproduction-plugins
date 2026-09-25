#include "../../GILLCommon/QualityBus.h"
#if defined(_WIN32)
 #define EXPORT extern "C" __declspec(dllexport)
#else
 #define EXPORT extern "C" __attribute__((visibility("default")))
#endif
EXPORT void* qualityCreate(int controller) { return new gill::quality_detail::Bus(controller != 0); }
EXPORT void qualityDestroy(void* pointer) { delete static_cast<gill::quality_detail::Bus*>(pointer); }
EXPORT int qualityPoll(void* pointer, int* mode, std::uint64_t* sequence) {
    const auto state = static_cast<gill::quality_detail::Bus*>(pointer)->poll();
    *mode = state.mode; *sequence = state.sequence;
    return state.valid ? state.instances : -1;
}
EXPORT int qualityBroadcast(void* pointer, int mode) { return static_cast<gill::quality_detail::Bus*>(pointer)->broadcast(mode) ? 1 : 0; }
EXPORT void qualityExpire(void* pointer) { static_cast<gill::quality_detail::Bus*>(pointer)->expireForTest(); }
