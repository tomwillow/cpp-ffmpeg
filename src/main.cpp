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

private:
    FILE *fp;
    std::string filename;
};

class MyStream {
public:
    MyStream(AVFormatContext *ctx, int streamIndex) noexcept : ctx(ctx), streamIndex(streamIndex) {}

private:
    AVFormatContext *ctx;
    int streamIndex;
};

class MyAVFormatContext {
public:
    MyAVFormatContext(const std::string &filename) : ctx(nullptr, avformat_free_context) {
        AVFormatContext *rawCtx = nullptr;
        int err = avformat_open_input(&rawCtx, filename.c_str(), NULL, NULL);
        if (err < 0) {
            throw std::runtime_error(fmt::format("failed to avformat_open_input. err = {}", err));
        }

        ctx.reset(rawCtx);
    }

    MyStream FindAudioStream() const {
        int streamIndex = av_find_best_stream(ctx.get(), AVMediaType::AVMEDIA_TYPE_AUDIO, -1, -1, NULL, NULL);
        return MyStream(ctx.get(), streamIndex);
    }

    void Fun() {
        const AVCodec *codec = NULL;
        int steamIndex = av_find_best_stream(ctx.get(), AVMediaType::AVMEDIA_TYPE_AUDIO, -1, -1, &codec, NULL);
        if (steamIndex < 0) {
            throw std::runtime_error(fmt::format("failed to av_find_best_stream. err = {}", steamIndex));
        }

        AVPacket pkt;
        av_init_packet(&pkt);

        MyFile file("out.aac", "wb");

        while (1) {
            int err = av_read_frame(ctx.get(), &pkt);
            if (err < 0) {
                if (err == AVERROR_EOF) {
                    // spdlog::info("end.");
                    break;
                }
                throw std::runtime_error(fmt::format("failed to av_read_frame. err = {}, {}", err, GetErrorInfo(err)));
            }

            // success

            if (pkt.stream_index != steamIndex) {
                av_packet_unref(&pkt);
                continue;
            }

            file.Write(pkt.data, pkt.size);

            av_packet_unref(&pkt);
        }
    }

private:
    std::unique_ptr<AVFormatContext, void (*)(AVFormatContext *)> ctx;
};

class MyOutputContext {
public:
    MyOutputContext(const std::string &outputFileName) : ctx(nullptr, [](AVFormatContext *) {}) {
        // const char *codecName = avcodec_get_name(ctx->streams[1]->codecpar->codec_id);

        // const AVOutputFormat *outFormat = av_guess_format(NULL, (std::string(".") + codecName).c_str(), NULL);
        // if (outFormat == nullptr) {
        //     throw std::runtime_error(fmt::format("failed to av_guess_format. "));
        // }

        // AVFormatContext *outCtx = nullptr;
        // err = avformat_alloc_output_context2(&outCtx, outFormat, NULL, NULL);
        // if (err < 0) {
        //     throw std::runtime_error(
        //         fmt::format("failed to avformat_alloc_output_context2. err = {}, {}", err, GetErrorInfo(err)));
        // }

        //// av_dump_format(outCtx->oformat, 0, "tesss", 1);

        // avformat_free_context(outCtx);
    }

private:
    std::unique_ptr<AVFormatContext, void (*)(AVFormatContext *)> ctx;
};

int main() {
    // 开启运行时内存检查
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);

    try {
        av_log_set_level(AV_LOG_ERROR);

        MemoryLeakDetector mld;

        MyAVFormatContext ctx("test.mp4");
        // ctx.Fun();
    } catch (const std::runtime_error &err) {
        spdlog::error("{}", err.what());
        return -1;
    }

    return 0;
}