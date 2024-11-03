#pragma once

extern "C" {
#include <libavutil/log.h>
#include <libavcodec/codec.h>
#include <libavformat/avformat.h>
}

#include <fmt/format.h>

#include <memory>
#include <string>
#include <cassert>
#include <stdexcept>
#include <functional>

inline std::string GetErrorInfo(int err) noexcept {
    char msg[AV_ERROR_MAX_STRING_SIZE];
    av_make_error_string(msg, AV_ERROR_MAX_STRING_SIZE, err);
    return msg;
}

class StreamRef {
public:
    StreamRef(AVStream *stream) noexcept : stream(stream) {}

    StreamRef(AVStream *stream, AVFormatContext *outputCtx) {
        AVStream *s = avformat_new_stream(outputCtx, NULL);
        if (s == nullptr) {
            throw std::bad_alloc();
        }

        int err = avcodec_parameters_copy(s->codecpar, stream->codecpar);
        if (err < 0) {
            throw std::runtime_error(
                fmt::format("failed to avcodec_parameters_copy. err = {}, {}", err, GetErrorInfo(err)));
        }
    }

    StreamRef(const StreamRef &rhs) noexcept {
        *this = rhs;
    }
    StreamRef &operator=(const StreamRef &rhs) noexcept {
        stream = rhs.stream;
        return *this;
    }

    bool operator==(const StreamRef &rhs) const noexcept {
        return stream == rhs.stream;
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

namespace std {
template <>
struct hash<StreamRef> {
    std::size_t operator()(const StreamRef &stream) const noexcept {
        return reinterpret_cast<std::size_t>(stream.GetAVStream());
    }
};
} // namespace std

class Demuxer {
public:
    Demuxer(const std::string &filename)
        : ctx(nullptr, [](AVFormatContext *rawCtx) {
              avformat_close_input(&rawCtx);
          }) {
        AVFormatContext *rawCtx = nullptr;
        int err = avformat_open_input(&rawCtx, filename.c_str(), NULL, NULL);
        if (err < 0) {
            throw std::runtime_error(fmt::format("failed to avformat_open_input. err = {}", err));
        }

        ctx.reset(rawCtx);

        for (int i = 0; i < ctx->nb_streams; ++i) {
            switch (ctx->streams[i]->codecpar->codec_type) {
            case AVMediaType::AVMEDIA_TYPE_VIDEO:
                videoStreams.push_back(StreamRef(ctx->streams[i]));
                break;
            case AVMediaType::AVMEDIA_TYPE_AUDIO:
                audioStreams.push_back(StreamRef(ctx->streams[i]));
                break;
            case AVMediaType::AVMEDIA_TYPE_SUBTITLE:
                subtitleStreams.push_back(StreamRef(ctx->streams[i]));
                break;
            default:
                otherStreams.push_back(StreamRef(ctx->streams[i]));
                break;
            }
        }
    }

    AVFormatContext *Raw() noexcept {
        return ctx.get();
    }

    void DumpFormat() const noexcept {
        av_dump_format(ctx.get(), 0, NULL, 0);
    }

    StreamRef FindBestStream(AVMediaType mediaType) const noexcept {
        int streamIndex = av_find_best_stream(ctx.get(), mediaType, -1, -1, NULL, NULL);
        return StreamRef(ctx->streams[streamIndex]);
    }

    StreamRef FindBestAudioStream() const noexcept {
        return FindBestStream(AVMediaType::AVMEDIA_TYPE_AUDIO);
    }

    StreamRef FindBestVideoStream() const noexcept {
        return FindBestStream(AVMediaType::AVMEDIA_TYPE_VIDEO);
    }

    void TraversalPacket(std::function<void(AVPacket &pkt, const StreamRef &inputStream)> fn) const {
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

            fn(pkt, StreamRef(ctx->streams[pkt.stream_index]));

            av_packet_unref(&pkt);
        }
    }

private:
    std::unique_ptr<AVFormatContext, void (*)(AVFormatContext *)> ctx;
    std::vector<StreamRef> videoStreams;
    std::vector<StreamRef> audioStreams;
    std::vector<StreamRef> subtitleStreams;
    std::vector<StreamRef> otherStreams;
};

class Muxer {
public:
    /**
     * @exception
     */
    Muxer(const std::string &outputFileName) : outputFileName(outputFileName), ctx(nullptr, avformat_free_context) {
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

    /**
     * @exception
     */
    const StreamRef &AddNewStream(const AVCodecParameters &param) {
        AVStream *s = avformat_new_stream(ctx.get(), NULL);

        int err = avcodec_parameters_copy(s->codecpar, &param);
        if (err < 0) {
            throw std::runtime_error(
                fmt::format("failed to avcodec_parameters_copy. err = {}, {}", err, GetErrorInfo(err)));
        }
        s->codecpar->codec_tag = 0;
        StreamRef ret(s);

        switch (param.codec_type) {
        case AVMediaType::AVMEDIA_TYPE_VIDEO:
            videoStreams.push_back(ret);
            break;
        case AVMediaType::AVMEDIA_TYPE_AUDIO:
            audioStreams.push_back(ret);
            break;
        case AVMediaType::AVMEDIA_TYPE_SUBTITLE:
            subtitleStreams.push_back(ret);
            break;
        default:
            otherStreams.push_back(ret);
            break;
        }
        return ret;
    }

    void CopyStream(const StreamRef &otherStream) {
        StreamRef newStreamRef = AddNewStream(otherStream.GetAVCodecParameters());
        otherStreamsToMyStreams.insert({otherStream, newStreamRef});
    }

    void WriteToFile(const Demuxer &inputCtx) const {
        if (!(ctx->flags & AVFMT_NOFILE)) {
            int err = avio_open(&ctx->pb, outputFileName.c_str(), AVIO_FLAG_WRITE);
            if (err < 0) {
                throw std::runtime_error(
                    fmt::format("failed to avcodec_parameters_copy. err = {}, {}", err, GetErrorInfo(err)));
            }
        }

        int err = avformat_write_header(ctx.get(), NULL);
        if (err < 0) {
            throw std::runtime_error(
                fmt::format("failed to avformat_write_header. err = {}, {}", err, GetErrorInfo(err)));
        }

        inputCtx.TraversalPacket([this](AVPacket &pkt, const StreamRef &inputStream) {
            if (otherStreamsToMyStreams.count(inputStream) == 0) {
                return;
            }

            pkt.stream_index = otherStreamsToMyStreams.at(inputStream).GetAVStream()->index;
            AVStream *out_stream = ctx.get()->streams[pkt.stream_index];
            // log_packet(ifmt_ctx.Raw(), &pkt, "in");

            /* copy packet */
            pkt.pts = av_rescale_q_rnd(pkt.pts, inputStream.GetTimeBase(), out_stream->time_base,
                                       AVRounding(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
            pkt.dts = av_rescale_q_rnd(pkt.dts, inputStream.GetTimeBase(), out_stream->time_base,
                                       AVRounding(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
            pkt.duration = av_rescale_q(pkt.duration, inputStream.GetTimeBase(), out_stream->time_base);
            pkt.pos = -1;
            // log_packet(ctx.get(), &pkt, "out");

            int err = av_interleaved_write_frame(ctx.get(), &pkt);
            if (err < 0) {
                throw std::runtime_error(
                    fmt::format("failed to av_interleaved_write_frame. err = {}, {}", err, GetErrorInfo(err)));
            }
        });

        av_write_trailer(ctx.get());

        /* close output */
        if (!(ctx->flags & AVFMT_NOFILE))
            avio_closep(&ctx->pb);
    }

private:
    const std::string &outputFileName;
    std::unique_ptr<AVFormatContext, void (*)(AVFormatContext *)> ctx;
    std::vector<StreamRef> videoStreams;
    std::vector<StreamRef> audioStreams;
    std::vector<StreamRef> subtitleStreams;
    std::vector<StreamRef> otherStreams;

    std::unordered_map<StreamRef, StreamRef> otherStreamsToMyStreams;
};