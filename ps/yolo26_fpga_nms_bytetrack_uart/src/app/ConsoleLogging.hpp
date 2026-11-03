#pragma once
#include <cstdint>

namespace xunjing {
enum class ConsoleStage { Input, Npu, Display };
// Independent per-stage/per-thread counters; CSV timing stays per-frame.
template<ConsoleStage Stage> inline bool ConsoleTimingDue() {
    thread_local std::uint64_t frames = 0;
    return ++frames % 30 == 0;
}
}
