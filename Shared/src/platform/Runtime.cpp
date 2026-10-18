// runtime.cpp  -  implementations for the portable runtime layer.
#include "Runtime.h"

namespace runtime {

std::recursive_mutex& EngineLock() {
    static std::recursive_mutex m;
    return m;
}

} // namespace runtime
