#pragma once
#include <fmt/format.h>

extern "C" {
#include <libavutil/log.h>
#include <libavcodec/codec.h>
#include <libavformat/avformat.h>
}

#include <memory>
#include <array>
#include <string>
#include <cassert>
#include <stdexcept>
#include <functional>

inline std::string GetErrorInfo(int err) noexcept {
    char msg[AV_ERROR_MAX_STRING_SIZE];
    av_make_error_string(msg, AV_ERROR_MAX_STRING_SIZE, err);
    return msg;
}

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

    AVFormatContext *Raw() noexcept {
        return ctx.get();
    }

    void DumpFormat() const noexcept {
        av_dump_format(ctx.get(), 0, NULL, 0);
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

    AVFormatContext *Raw() noexcept {
        return ctx.get();
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