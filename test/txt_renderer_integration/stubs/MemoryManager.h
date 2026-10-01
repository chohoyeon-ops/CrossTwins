#pragma once
#include <cstddef>
#include <functional>
namespace freeink { class MemoryManager { public:
 struct Sink { const char* name; int priority; std::function<size_t(size_t)> fn; };
 static MemoryManager& instance() { static MemoryManager m; return m; }
 void registerSink(Sink) {} size_t freeBytes() { return 200*1024; }
}; }
