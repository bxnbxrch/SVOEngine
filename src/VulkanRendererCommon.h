#pragma once

#include <iostream>
#include <sstream>

#ifdef DEBUG
struct DebugStream {
    std::ostringstream oss;
    template<typename T>
    DebugStream& operator<<(const T& x) {
        oss << x;
        return *this;
    }
    ~DebugStream() {
        if (!oss.str().empty()) {
            std::cout << oss.str() << std::flush;
        }
    }
};
#define DBGPRINT DebugStream()
#else
struct NullStream {
    template<typename T>
    NullStream& operator<<(const T&) { return *this; }
};
#define DBGPRINT NullStream()
#endif
