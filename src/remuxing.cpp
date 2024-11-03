

#include "memory_leak_detector.h"
#include "ffmpeg_wrapper.h"

#include <spdlog/spdlog.h>

int main(int argc, char **argv) {
    MemoryLeakDetector mld;

    try {
        if (argc < 3) {
            printf("usage: %s input output\n"
                   "API example program to remux a media file with libavformat and libavcodec.\n"
                   "The output format is guessed according to the file extension.\n"
                   "\n",
                   argv[0]);
            return 1;
        }

        std::string inputFileName = argv[1];
        std::string outputFileName = argv[2];

        Demuxer demuxer(inputFileName);

        demuxer.DumpFormat();

        Muxer muxer(outputFileName);

        muxer.CopyStream(demuxer.FindBestVideoStream());
        muxer.CopyStream(demuxer.FindBestAudioStream());

        muxer.DumpFormat();

        muxer.WriteToFile(demuxer);
    } catch (const std::runtime_error &err) {
        spdlog::error("{}", err.what());
        return -1;
    }

    return 0;
}
