#pragma once

#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

#include <cassert>

class MemoryLeakDetector {
public:
    MemoryLeakDetector() noexcept {
        _CrtMemCheckpoint(&s1);
    }

    ~MemoryLeakDetector() {
        _CrtMemState s2;
        _CrtMemState s3;
        _CrtMemCheckpoint(&s2);
        if (_CrtMemDifference(&s3, &s1, &s2)) {
            _CrtMemDumpStatistics(&s3);
            assert(0 && "Memory leak is detected!");
        }
    }

private:
    _CrtMemState s1;
};