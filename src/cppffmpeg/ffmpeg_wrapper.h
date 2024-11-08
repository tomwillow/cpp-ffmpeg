#pragma once

extern "C" {
#include <libavutil/log.h>
#include <libavcodec/codec.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include "libavutil/imgutils.h"
#include "libswscale/swscale.h"
}

#include <fmt/format.h>
#include <spdlog/spdlog.h>

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

double timestr(int64_t ts, const AVRational &tb) noexcept {
    return av_q2d(tb) * ts;
}

static void log_packet(const AVRational &time_base, const AVPacket *pkt, const char *tag) {
    spdlog::info("{}: pts:{} pts_time:{} dts:{} dts_time:{} duration:{} duration_time:{} stream_index:{}", tag,
                 pkt->pts, timestr(pkt->pts, time_base), pkt->dts, timestr(pkt->dts, time_base), pkt->duration,
                 timestr(pkt->duration, time_base), pkt->stream_index);
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

    bool operator!=(const StreamRef &rhs) const noexcept {
        return stream != rhs.stream;
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
        : filename(filename), ctx(nullptr, [](AVFormatContext *rawCtx) {
              avformat_close_input(&rawCtx);
          }) {
        AVFormatContext *rawCtx = nullptr;
        int err = avformat_open_input(&rawCtx, filename.c_str(), NULL, NULL);
        if (err < 0) {
            throw std::runtime_error(
                fmt::format("failed to avformat_open_input. err = {}, {}", err, GetErrorInfo(err)));
        }

        ctx.reset(rawCtx);

        err = avformat_find_stream_info(ctx.get(), NULL);
        if (err < 0) {
            throw std::runtime_error(
                fmt::format("failed to avformat_find_stream_info. err = {}, {}", err, GetErrorInfo(err)));
        }

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

    Demuxer(const Demuxer &rhs) = delete;
    Demuxer &operator=(const Demuxer &rhs) = delete;

    Demuxer(Demuxer &&rhs) noexcept
        : ctx(nullptr, [](AVFormatContext *rawCtx) {
              avformat_close_input(&rawCtx);
          }) {
        this->operator=(std::move(rhs));
    }

    Demuxer &operator=(Demuxer &&rhs) noexcept {
        if (this == &rhs) {
            return *this;
        }
        std::swap(filename, rhs.filename);
        std::swap(ctx, rhs.ctx);
        std::swap(videoStreams, rhs.videoStreams);
        std::swap(audioStreams, rhs.audioStreams);
        std::swap(subtitleStreams, rhs.subtitleStreams);
        std::swap(otherStreams, rhs.otherStreams);
        return *this;
    }

    bool operator==(const Demuxer &rhs) const noexcept {
        return ctx.get() == rhs.ctx.get();
    }

    AVFormatContext *Raw() noexcept {
        return ctx.get();
    }

    const AVFormatContext *Raw() const noexcept {
        return ctx.get();
    }

    std::unordered_map<std::string, std::string> GetInfo() const {
        std::unordered_map<std::string, std::string> ret;
        const AVDictionary *m = ctx->metadata;
        const AVDictionaryEntry *tag = NULL;

        while (1) {
            tag = av_dict_iterate(m, tag);
            if (tag == nullptr) {
                break;
            }
        }

        return ret;
    }

    void DumpFormat() const noexcept {
        av_dump_format(ctx.get(), 0, filename.c_str(), 0);
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
    std::string filename;
    std::unique_ptr<AVFormatContext, void (*)(AVFormatContext *)> ctx;
    std::vector<StreamRef> videoStreams;
    std::vector<StreamRef> audioStreams;
    std::vector<StreamRef> subtitleStreams;
    std::vector<StreamRef> otherStreams;
};

namespace std {
template <>
struct hash<Demuxer> {
    std::size_t operator()(const Demuxer &demuxer) const noexcept {
        return reinterpret_cast<std::size_t>(demuxer.Raw());
    }
};
} // namespace std

bool operator==(const std::reference_wrapper<const Demuxer> &lhs,
                const std::reference_wrapper<const Demuxer> &rhs) noexcept {
    return lhs.get() == rhs.get();
}

namespace std {
template <>
struct hash<std::reference_wrapper<const Demuxer>> {
    std::size_t operator()(const std::reference_wrapper<const Demuxer> &demuxer) const noexcept {
        return reinterpret_cast<std::size_t>(demuxer.get().Raw());
    }
};
} // namespace std

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
        av_dump_format(ctx.get(), 0, outputFileName.c_str(), 1);
    }

    /**
     * @exception
     */
    StreamRef AddNewStream(const AVCodecParameters &param) {
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

    void CopyStream(const Demuxer &inputCtx, const StreamRef &otherStream) {
        StreamRef newStreamRef = AddNewStream(otherStream.GetAVCodecParameters());
        inputCtxAndStreamMapping[std::ref(inputCtx)].insert(std::make_pair(otherStream, newStreamRef));
    }

    void WriteToFile() {
        OpenAndWriteHeader();

        for (auto &[inputCtx, streamMapping] : inputCtxAndStreamMapping) {

            inputCtx.get().TraversalPacket([this, &streamMapping](AVPacket &pkt, const StreamRef &currentInputStream) {
                if (streamMapping.count(currentInputStream) == 0) {
                    return;
                }

                auto &outputStream = streamMapping.at(currentInputStream);
                pkt.stream_index = outputStream.GetAVStream()->index;
                // log_packet(currentInputStream.GetTimeBase(), &pkt, "in");

                /* copy packet */
                pkt.pts = av_rescale_q_rnd(pkt.pts, currentInputStream.GetTimeBase(), outputStream.GetTimeBase(),
                                           AVRounding(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
                pkt.dts = av_rescale_q_rnd(pkt.dts, currentInputStream.GetTimeBase(), outputStream.GetTimeBase(),
                                           AVRounding(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
                pkt.duration = av_rescale_q(pkt.duration, currentInputStream.GetTimeBase(), outputStream.GetTimeBase());
                pkt.pos = -1;
                // log_packet(outStream.GetTimeBase(), &pkt, "out");

                int err = av_interleaved_write_frame(ctx.get(), &pkt);
                if (err < 0) {
                    throw std::runtime_error(
                        fmt::format("failed to av_interleaved_write_frame. err = {}, {}", err, GetErrorInfo(err)));
                }
            });
        }
        WriteTrailerAndClose();
    }

private:
    std::string outputFileName;
    std::unique_ptr<AVFormatContext, void (*)(AVFormatContext *)> ctx;
    std::vector<StreamRef> videoStreams;
    std::vector<StreamRef> audioStreams;
    std::vector<StreamRef> subtitleStreams;
    std::vector<StreamRef> otherStreams;

    std::unordered_map<std::reference_wrapper<const Demuxer>, std::unordered_map<StreamRef, StreamRef>>
        inputCtxAndStreamMapping;

    void OpenAndWriteHeader() {
        if (!(ctx->flags & AVFMT_NOFILE)) {
            int err = avio_open(&ctx->pb, outputFileName.c_str(), AVIO_FLAG_WRITE);
            if (err < 0) {
                throw std::runtime_error(fmt::format("failed to avio_open. err = {}, {}", err, GetErrorInfo(err)));
            }
        }

        int err = avformat_write_header(ctx.get(), NULL);
        if (err < 0) {
            throw std::runtime_error(
                fmt::format("failed to avformat_write_header. err = {}, {}", err, GetErrorInfo(err)));
        }
    }

    void WriteTrailerAndClose() {
        av_write_trailer(ctx.get());

        /* close output */
        if (!(ctx->flags & AVFMT_NOFILE))
            avio_closep(&ctx->pb);
    }
};

class Writer {
public:
    AVPixelFormat outputFormat;
    int width, height;
    AVPixelFormat inputFormat;
    std::unique_ptr<FILE, std::function<void(FILE *)>> fp;
    std::unique_ptr<AVFrame, std::function<void(AVFrame *)>> pFrameYUV;
    std::unique_ptr<uint8_t, std::function<void(uint8_t *)>> out_buffer;
    std::unique_ptr<SwsContext, std::function<void(SwsContext *)>> img_convert_ctx;

    Writer(const AVCodecContext *pCodecCtx, const std::string &filename)
        : width(pCodecCtx->width), height(pCodecCtx->height), inputFormat(pCodecCtx->pix_fmt) {
        outputFormat = AVPixelFormat::AV_PIX_FMT_RGB24;

        fp = std::unique_ptr<FILE, std::function<void(FILE *)>>(fopen(filename.c_str(), "wb"), [](FILE *p) {
            fclose(p);
        });

        int bufferSize = av_image_get_buffer_size(outputFormat, width, height, 1);
        auto out_buffer_raw = (uint8_t *)av_malloc(bufferSize);

        pFrameYUV = std::unique_ptr<AVFrame, std::function<void(AVFrame *)>>(av_frame_alloc(), [](AVFrame *p) {
            av_frame_free(&p);
        });
        out_buffer = std::unique_ptr<uint8_t, std::function<void(uint8_t *)>>(out_buffer_raw, [](uint8_t *p) {
            av_free(p);
        });

        int sz = av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, out_buffer.get(), outputFormat, width,
                                      height, 1);
        if (sz < 0) {
            throw std::runtime_error("av_image_fill_arrays failed");
        }

        SwsContext *img_convert_ctx_raw =
            sws_getContext(width, height, inputFormat, width, height, outputFormat, SWS_BICUBIC, NULL, NULL, NULL);

        img_convert_ctx =
            std::unique_ptr<SwsContext, std::function<void(SwsContext *)>>(img_convert_ctx_raw, [](SwsContext *p) {
                sws_freeContext(p);
            });
    }

    void WriteFrame(const AVFrame *frame) {

        //
        sws_scale(img_convert_ctx.get(), (const uint8_t *const *)frame->data, frame->linesize, 0, frame->height,
                  pFrameYUV->data, pFrameYUV->linesize);

        if (outputFormat == AVPixelFormat::AV_PIX_FMT_YUV420P) {
            fwrite(pFrameYUV->data[0], pFrameYUV->linesize[0] * frame->height, 1, fp.get());
            fwrite(pFrameYUV->data[1], width * frame->height / 4, 1, fp.get());
            fwrite(pFrameYUV->data[2], width * frame->height / 4, 1, fp.get());
            return;
        }

        if (outputFormat == AVPixelFormat::AV_PIX_FMT_RGB24) {
            // if (!frame->key_frame) {
            //	return;
            // }

            // static int i = 0;
            // string filename = std::to_string(i) + ".png";

            // stbi_write_png(filename.c_str(), pCodecCtx->width, pCodecCtx->height, 3, pFrameYUV->data[0], 0);
            fwrite(pFrameYUV->data[0], pFrameYUV->linesize[0] * frame->height, 1, fp.get());
            // i++;
            return;
        }

        assert(0);
    }
};

class MyDecoder {
public:
    MyDecoder(Demuxer &demuxer, StreamRef stream) : demuxer(demuxer), stream(stream) {

        const AVCodec *pCodec = avcodec_find_decoder(stream.GetAVStream()->codecpar->codec_id);
        if (pCodec == NULL) {
            throw std::runtime_error(fmt::format("failed to avcodec_find_decoder. codec_id = {}",
                                                 static_cast<int>(stream.GetAVStream()->codecpar->codec_id)));
        }

        pCodecCtx = std::unique_ptr<AVCodecContext, std::function<void(AVCodecContext *)>>(
            avcodec_alloc_context3(pCodec), [](AVCodecContext *p) {
                avcodec_free_context(&p);
            });

        int ok = avcodec_parameters_to_context(pCodecCtx.get(), stream.GetAVStream()->codecpar);
        if (ok < 0) {
            throw std::runtime_error("avcodec_parameters_to_context failed");
        }

        if (avcodec_open2(pCodecCtx.get(), pCodec, NULL) < 0) {
            throw std::runtime_error("Could not open codec");
        }

        pFrame = std::unique_ptr<AVFrame, std::function<void(AVFrame *)>>(av_frame_alloc(), [](AVFrame *p) {
            av_frame_free(&p);
        });
    }

    void WriteYUV() {

        Writer writer(pCodecCtx.get(), "a.rgb");

        int frame_cnt = 0;
        demuxer.TraversalPacket([this, &writer, &frame_cnt](AVPacket &pkt, const StreamRef &inputStream) {
            if (stream != inputStream) {
                return;
            }

            Decode(&pkt, [&writer](const AVFrame *frame) {
                writer.WriteFrame(frame);
            });

            printf("Decoded frame index: %d\n", frame_cnt);
            frame_cnt++;
        });

        Decode(NULL, [&writer](const AVFrame *frame) {
            writer.WriteFrame(frame);
        });
    }

private:
    Demuxer &demuxer;
    StreamRef stream;
    std::unique_ptr<AVCodecContext, std::function<void(AVCodecContext *)>> pCodecCtx;
    std::unique_ptr<AVFrame, std::function<void(AVFrame *)>> pFrame;

    void Decode(const AVPacket *packet, std::function<void(const AVFrame *frame)> fn) {

        int ret = avcodec_send_packet(pCodecCtx.get(), packet);
        if (ret != 0) {
            throw std::runtime_error("avcodec_send_packet failed");
        }

        while (1) {
            ret = avcodec_receive_frame(pCodecCtx.get(), pFrame.get());
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                return;
            else if (ret < 0) {
                throw std::runtime_error("avcodec_receive_frame failed");
            }

            fn(pFrame.get());
        }
    }
};