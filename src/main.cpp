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

class MyAVFormatContext {
public:
    MyAVFormatContext(const std::string &filename) {
        int err = avformat_open_input(&ctx, filename.c_str(), NULL, NULL);
        if (err < 0) {
            throw std::runtime_error(fmt::format("failed to avformat_open_input. err = {}", err));
        }
    }

    ~MyAVFormatContext() {
        avformat_close_input(&ctx);
    }

    void Fun() {
        const AVCodec *codec = NULL;
        int steamIndex = av_find_best_stream(ctx, AVMediaType::AVMEDIA_TYPE_AUDIO, -1, -1, &codec, NULL);
        if (steamIndex < 0) {
            throw std::runtime_error(fmt::format("failed to av_find_best_stream. err = {}", steamIndex));
        }

        AVPacket pkt;
        av_init_packet(&pkt);

        MyFile file("out.aac", "wb");

        while (1) {
            int err = av_read_frame(ctx, &pkt);
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
    AVFormatContext *ctx{NULL};
};

int main() {
    _CrtMemState s1;
    _CrtMemState s2;
    _CrtMemState s3;

    try {
        _CrtMemCheckpoint(&s1);
        MyAVFormatContext ctx("test.mp4");
        ctx.Fun();
    } catch (const std::runtime_error &err) {
        spdlog::error("{}", err.what());
        return -1;
    }
    // memory allocations take place here
    _CrtMemCheckpoint(&s2);

    if (_CrtMemDifference(&s3, &s1, &s2)) {
        _CrtMemDumpStatistics(&s3);
        assert(0);
    }

    return 0;
}