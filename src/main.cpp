

#include "memory_leak_detector.h"
#include "ffmpeg_wrapper.h"
#include "my_file.h"
#include "adts_header.h"

#include <spdlog/spdlog.h>

int main2() {
    MemoryLeakDetector mld;

    try {

        Demuxer input("test.mp4");

        auto &audioStream = input.FindAudioStream();

        MyFile file("out.aac", "wb");
        input.TraversalPacket([&file, &audioStream](AVPacket &pkt, const MyStream &inputStream) {
            if (pkt.stream_index != audioStream.GetAVStream()->index) {
                return;
            }

            assert(inputStream.GetAVCodecParameters().codec_type == AVMediaType::AVMEDIA_TYPE_AUDIO);

            auto adtsHeader = adts_header(pkt.size, audioStream.GetAVCodecParameters().sample_rate);

            file.Write(adtsHeader);

            file.Write(pkt.data, pkt.size);
        });

        // Muxer output("out." + audioStream.GetCodecName());
        // Muxer output("out.flv");

        // output.AddNewStream(audioStream.GetAVCodecParameters());

        // output.DumpFormat();

        // output.WriteToFile(input);

        // input.Fun();
    } catch (const std::runtime_error &err) {
        spdlog::error("{}", err.what());
        return -1;
    }

    return 0;
}