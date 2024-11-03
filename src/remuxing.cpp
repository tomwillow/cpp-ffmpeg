
#include "ffmpeg_wrapper.h"

#include <spdlog/spdlog.h>

double timestr(int64_t ts, const AVRational &tb) noexcept {
    return av_q2d(tb) * ts;
}

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt, const char *tag) {
    const AVRational &time_base = fmt_ctx->streams[pkt->stream_index]->time_base;

    spdlog::info("{}: pts:{} pts_time:{} dts:{} dts_time:{} duration:{} duration_time:{} stream_index:{}", tag,
                 pkt->pts, timestr(pkt->pts, time_base), pkt->dts, timestr(pkt->dts, time_base), pkt->duration,
                 timestr(pkt->duration, time_base), pkt->stream_index);
}

int main(int argc, char **argv) {

    int ret, i;

    if (argc < 3) {
        printf("usage: %s input output\n"
               "API example program to remux a media file with libavformat and libavcodec.\n"
               "The output format is guessed according to the file extension.\n"
               "\n",
               argv[0]);
        return 1;
    }

    std::string in_filename = argv[1];
    std::string out_filename = argv[2];

    Demuxer ifmt_ctx(in_filename);

    // if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0) {
    //     fprintf(stderr, "Failed to retrieve input stream information");
    //     goto end;
    // }

    ifmt_ctx.DumpFormat();

    Muxer ofmt_ctx(out_filename);

    std::vector<int> streamMapping(ifmt_ctx.Raw()->nb_streams, -1);

    const AVOutputFormat *ofmt = ofmt_ctx.Raw()->oformat;

    int stream_index = 0;
    for (i = 0; i < ifmt_ctx.Raw()->nb_streams; i++) {
        AVStream *in_stream = ifmt_ctx.Raw()->streams[i];
        AVCodecParameters *in_codecpar = in_stream->codecpar;

        if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO && in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
            streamMapping[i] = -1;
            continue;
        }

        streamMapping[i] = stream_index++;

        AVStream *out_stream = avformat_new_stream(ofmt_ctx.Raw(), NULL);
        if (!out_stream) {
            fprintf(stderr, "Failed allocating output stream\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        int err = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
        if (err < 0) {
            throw std::runtime_error(
                fmt::format("failed to avcodec_parameters_copy. err = {}, {}", err, GetErrorInfo(err)));
        }
        out_stream->codecpar->codec_tag = 0;
    }

    ofmt_ctx.DumpFormat();

    if (!(ofmt->flags & AVFMT_NOFILE)) {
        int err = avio_open(&ofmt_ctx.Raw()->pb, out_filename.c_str(), AVIO_FLAG_WRITE);
        if (err < 0) {
            throw std::runtime_error(
                fmt::format("failed to avcodec_parameters_copy. err = {}, {}", err, GetErrorInfo(err)));
        }
    }

    int err = avformat_write_header(ofmt_ctx.Raw(), NULL);
    if (err < 0) {
        throw std::runtime_error(fmt::format("failed to avformat_write_header. err = {}, {}", err, GetErrorInfo(err)));
    }

    ifmt_ctx.TraversalPacket([&streamMapping, &ofmt_ctx](AVPacket &pkt, const MyStream &inputStream) {
        const AVStream *in_stream = inputStream.GetAVStream();

        if (pkt.stream_index >= streamMapping.size() || streamMapping[pkt.stream_index] < 0) {
            return;
        }

        pkt.stream_index = streamMapping[pkt.stream_index];
        AVStream *out_stream = ofmt_ctx.Raw()->streams[pkt.stream_index];
        // log_packet(ifmt_ctx.Raw(), &pkt, "in");

        /* copy packet */
        pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base,
                                   AVRounding(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base,
                                   AVRounding(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
        pkt.pos = -1;
        log_packet(ofmt_ctx.Raw(), &pkt, "out");

        int err = av_interleaved_write_frame(ofmt_ctx.Raw(), &pkt);
        if (err < 0) {
            throw std::runtime_error(
                fmt::format("failed to av_interleaved_write_frame. err = {}, {}", err, GetErrorInfo(err)));
        }
    });

    av_write_trailer(ofmt_ctx.Raw());
end:

    /* close output */
    if (ofmt_ctx.Raw() && !(ofmt->flags & AVFMT_NOFILE))
        avio_closep(&ofmt_ctx.Raw()->pb);

    return 0;
}
