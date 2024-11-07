

#include "cppffmpeg/memory_leak_detector.h"
#include "cppffmpeg/ffmpeg_wrapper.h"

#include <spdlog/spdlog.h>

int main(int argc, char **argv) {
    MemoryLeakDetector mld;

    try {

        Demuxer demuxer("tik.flv");
        demuxer.DumpFormat();

        MyDecoder decoder(demuxer, demuxer.FindBestVideoStream());

        decoder.WriteYUV();

    } catch (const std::runtime_error &err) {
        spdlog::error("{}", err.what());
        return -1;
    }

    return 0;
}
