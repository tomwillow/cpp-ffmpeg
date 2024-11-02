#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>

#include <spdlog/spdlog.h>

extern "C" {
#include <libavutil/log.h>
#include <libavcodec/codec.h>
#include <libavformat/avformat.h>
}

#include <memory>
#include <array>

std::string GetErrorInfo(int err) noexcept {
    char msg[AV_ERROR_MAX_STRING_SIZE];
    av_make_error_string(msg, AV_ERROR_MAX_STRING_SIZE, err);
    return msg;
}

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

int ADTSSamplingFrequencyIndex(int samplingFrequency) noexcept {
    switch (samplingFrequency) {
    case 48000:
        return 3;
    case 44100:
        return 4;
    }
    return 0xf;
}

std::array<char, 7> adts_header(int dataLen, int samplingFrequency) {
    std::array<char, 7> ret;
    char *szAdtsHeader = ret.data();

    int audio_object_type = 2;
    int sampling_frequency_index = ADTSSamplingFrequencyIndex(samplingFrequency);
    int channel_config = 2;

    int adtsLen = dataLen + 7;

    szAdtsHeader[0] = 0xff;      // syncword:0xfff                          高8bits
    szAdtsHeader[1] = 0xf0;      // syncword:0xfff                          低4bits
    szAdtsHeader[1] |= (0 << 3); // MPEG Version:0 for MPEG-4,1 for MPEG-2  1bit
    szAdtsHeader[1] |= (0 << 1); // Layer:0                                 2bits
    szAdtsHeader[1] |= 1;        // protection absent:1                     1bit

    szAdtsHeader[2] = (audio_object_type - 1) << 6; // profile:audio_object_type - 1                      2bits
    szAdtsHeader[2] |= (sampling_frequency_index & 0x0f)
                       << 2;                         // sampling frequency index:sampling_frequency_index  4bits
    szAdtsHeader[2] |= (0 << 1);                     // private bit:0                                      1bit
    szAdtsHeader[2] |= (channel_config & 0x04) >> 2; // channel configuration:channel_config               高1bit

    szAdtsHeader[3] = (channel_config & 0x03) << 6; // channel configuration:channel_config      低2bits
    szAdtsHeader[3] |= (0 << 5);                    // original：0                               1bit
    szAdtsHeader[3] |= (0 << 4);                    // home：0                                   1bit
    szAdtsHeader[3] |= (0 << 3);                    // copyright id bit：0                       1bit
    szAdtsHeader[3] |= (0 << 2);                    // copyright id start：0                     1bit
    szAdtsHeader[3] |= ((adtsLen & 0x1800) >> 11);  // frame length：value   高2bits

    szAdtsHeader[4] = (uint8_t)((adtsLen & 0x7f8) >> 3); // frame length:value    中间8bits
    szAdtsHeader[5] = (uint8_t)((adtsLen & 0x7) << 5);   // frame length:value    低3bits
    szAdtsHeader[5] |= 0x1f;                             // buffer fullness:0x7ff 高5bits
    szAdtsHeader[6] = 0xfc;

    return ret;
}

class MyFile {
public:
    MyFile(const std::string &filename, const std::string &mode) : filename(filename) {
        fp = fopen(filename.c_str(), mode.c_str());
        if (!fp) {
            throw std::runtime_error(fmt::format("failed to open file", filename));
        }
    }

    ~MyFile() {
        fclose(fp);
    }

    void Write(const char *data, int size) {
        int wroteNum = fwrite(data, size, 1, fp);
        if (wroteNum != 1) {
            throw std::runtime_error(fmt::format("failed to write file", filename));
        }
    }

    void Write(const unsigned char *data, int size) {
        Write(reinterpret_cast<const char *>(data), size);
    }

    void Write(const std::vector<char> &buf) {
        Write(buf.data(), buf.size());
    }

    template <std::size_t N>
    void Write(const std::array<char, N> &buf) {
        Write(buf.data(), buf.size());
    }

private:
    FILE *fp;
    std::string filename;
};

class MyStream {
public:
    MyStream(AVStream *stream) noexcept : stream(stream) {}

    MyStream(AVStream *stream, AVFormatContext *outputCtx) {
        AVStream *s = avformat_new_stream(outputCtx, NULL);

        int err = avcodec_parameters_copy(s->codecpar, stream->codecpar);
        if (err < 0) {
            throw std::runtime_error(
                fmt::format("failed to avcodec_parameters_copy. err = {}, {}", err, GetErrorInfo(err)));
        }
    }

    AVRational GetTimeBase() const noexcept {
        return stream->time_base;
    }

    std::string GetCodecName() const noexcept {
        return avcodec_get_name(stream->codecpar->codec_id);
    }

    const AVCodecParameters &GetAVCodecParameters() const noexcept {
        return *stream->codecpar;
    }

    const AVStream *GetAVStream() const noexcept {
        return stream;
    }

private:
    AVStream *stream;
};

class MyAVFormatContext {
public:
    MyAVFormatContext(const std::string &filename)
        : ctx(nullptr, [](AVFormatContext *rawCtx) {
              avformat_close_input(&rawCtx);
          }) {
        AVFormatContext *rawCtx = nullptr;
        int err = avformat_open_input(&rawCtx, filename.c_str(), NULL, NULL);
        if (err < 0) {
            throw std::runtime_error(fmt::format("failed to avformat_open_input. err = {}", err));
        }

        ctx.reset(rawCtx);
    }

    const MyStream &FindAudioStream() const noexcept {
        if (audioStreams.empty()) {
            int streamIndex = av_find_best_stream(ctx.get(), AVMediaType::AVMEDIA_TYPE_AUDIO, -1, -1, NULL, NULL);
            audioStreams.push_back(MyStream(ctx->streams[streamIndex]));
        }
        return audioStreams[0];
    }

    void TraversalPacket(std::function<void(AVPacket &pkt, const MyStream &inputStream)> fn) const {
        AVPacket pkt;
        av_init_packet(&pkt);

        while (1) {
            int err = av_read_frame(ctx.get(), &pkt);
            if (err < 0) {
                if (err == AVERROR_EOF) {
                    break;
                }
                throw std::runtime_error(fmt::format("failed to av_read_frame. err = {}, {}", err, GetErrorInfo(err)));
            }

            // success

            fn(pkt, MyStream(ctx->streams[pkt.stream_index]));

            av_packet_unref(&pkt);
        }
    }

private:
    std::unique_ptr<AVFormatContext, void (*)(AVFormatContext *)> ctx;
    mutable std::vector<MyStream> audioStreams;
};

class MyOutputContext {
public:
    MyOutputContext(const std::string &outputFileName) : ctx(nullptr, avformat_free_context) {
        AVFormatContext *rawCtx = nullptr;
        int err = avformat_alloc_output_context2(&rawCtx, NULL, NULL, outputFileName.c_str());
        if (err < 0) {
            throw std::runtime_error(
                fmt::format("failed to avformat_alloc_output_context2. err = {}, {}", err, GetErrorInfo(err)));
        }

        ctx.reset(rawCtx);
    }

    void DumpFormat() const noexcept {
        av_dump_format(ctx.get(), 0, NULL, 1);
    }

    void AddNewStream(const AVCodecParameters &param) {
        AVStream *s = avformat_new_stream(ctx.get(), NULL);

        int err = avcodec_parameters_copy(s->codecpar, &param);
        if (err < 0) {
            throw std::runtime_error(
                fmt::format("failed to avcodec_parameters_copy. err = {}, {}", err, GetErrorInfo(err)));
        }
        // s->codecpar->codec_tag = 0;
        // s->time_base = AVRational{0, 0};
        // s->duration = 0;
        // s->start_time = 0;
        audioStreams.push_back(MyStream(s));
    }

    void WriteToFile(const MyAVFormatContext &inputCtx) const {
        int err = avformat_write_header(ctx.get(), NULL);
        if (err < 0) {
            throw std::runtime_error(
                fmt::format("failed to avformat_write_header. err = {}, {}", err, GetErrorInfo(err)));
        }

        const MyStream &outStream = audioStreams[0];
        inputCtx.TraversalPacket([this, &outStream](AVPacket &pkt, const MyStream &inputStream) {
            if (inputStream.GetAVCodecParameters().codec_type != AVMediaType::AVMEDIA_TYPE_AUDIO) {
                return;
            }

            // pkt.stream_index = 0;
            // pkt.pts = av_rescale_q_rnd(pkt.pts, inputStream.GetTimeBase(), outStream.GetTimeBase(),
            //                            AVRounding(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
            // pkt.dts = av_rescale_q_rnd(pkt.dts, inputStream.GetTimeBase(), outStream.GetTimeBase(),
            //                            AVRounding(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
            // pkt.duration = av_rescale_q(pkt.duration, inputStream.GetTimeBase(), outStream.GetTimeBase());
            // pkt.pos = -1;

            int err = av_interleaved_write_frame(ctx.get(), &pkt);
            if (err < 0) {
                throw std::runtime_error(
                    fmt::format("failed to av_interleaved_write_frame. err = {}, {}", err, GetErrorInfo(err)));
            }
        });

        err = av_write_trailer(ctx.get());
        if (err < 0) {
            throw std::runtime_error(fmt::format("failed to av_write_trailer. err = {}, {}", err, GetErrorInfo(err)));
        }
    }

private:
    std::unique_ptr<AVFormatContext, void (*)(AVFormatContext *)> ctx;
    std::vector<MyStream> audioStreams;
};

int main2() {
    MemoryLeakDetector mld;

    try {

        MyAVFormatContext input("test.mp4");

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

        // MyOutputContext output("out." + audioStream.GetCodecName());
        // MyOutputContext output("out.flv");

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