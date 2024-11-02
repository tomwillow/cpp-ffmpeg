
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

    AVPacket pkt;
    const char *in_filename, *out_filename;
    int ret, i;
    int stream_index = 0;

    if (argc < 3) {
        printf("usage: %s input output\n"
               "API example program to remux a media file with libavformat and libavcodec.\n"
               "The output format is guessed according to the file extension.\n"
               "\n",
               argv[0]);
        return 1;
    }

    in_filename = argv[1];
    out_filename = argv[2];

    MyAVFormatContext ifmt_ctx(argv[1]);

    // if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0) {
    //     fprintf(stderr, "Failed to retrieve input stream information");
    //     goto end;
    // }

    ifmt_ctx.DumpFormat();

    MyOutputContext ofmt_ctx(argv[2]);

    std::vector<int> streamMapping(ifmt_ctx.Raw()->nb_streams);

    const AVOutputFormat *ofmt = ofmt_ctx.Raw()->oformat;

    for (i = 0; i < ifmt_ctx.Raw()->nb_streams; i++) {
        AVStream *out_stream;
        AVStream *in_stream = ifmt_ctx.Raw()->streams[i];
        AVCodecParameters *in_codecpar = in_stream->codecpar;

        if (in_codecpar->codec_type != AVMEDIA_TYPE_AUDIO && in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
            streamMapping[i] = -1;
            continue;
        }

        streamMapping[i] = stream_index++;

        out_stream = avformat_new_stream(ofmt_ctx.Raw(), NULL);
        if (!out_stream) {
            fprintf(stderr, "Failed allocating output stream\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
        if (ret < 0) {
            fprintf(stderr, "Failed to copy codec parameters\n");
            goto end;
        }
        out_stream->codecpar->codec_tag = 0;
    }

    ofmt_ctx.DumpFormat();

    if (!(ofmt->flags & AVFMT_NOFILE)) {
        ret = avio_open(&ofmt_ctx.Raw()->pb, out_filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "Could not open output file '%s'", out_filename);
            goto end;
        }
    }

    ret = avformat_write_header(ofmt_ctx.Raw(), NULL);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file\n");
        goto end;
    }

    // ifmt_ctx.TraversalPacket([](AVPacket &pkt, const MyStream &inputStream) {

    //});

    while (1) {
        AVStream *in_stream, *out_stream;

        ret = av_read_frame(ifmt_ctx.Raw(), &pkt);
        if (ret < 0)
            break;

        in_stream = ifmt_ctx.Raw()->streams[pkt.stream_index];
        if (pkt.stream_index >= streamMapping.size() || streamMapping[pkt.stream_index] < 0) {
            av_packet_unref(&pkt);
            continue;
        }

        pkt.stream_index = streamMapping[pkt.stream_index];
        out_stream = ofmt_ctx.Raw()->streams[pkt.stream_index];
        log_packet(ifmt_ctx.Raw(), &pkt, "in");

        /* copy packet */
        pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base,
                                   AVRounding(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base,
                                   AVRounding(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
        pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
        pkt.pos = -1;
        log_packet(ofmt_ctx.Raw(), &pkt, "out");

        ret = av_interleaved_write_frame(ofmt_ctx.Raw(), &pkt);
        if (ret < 0) {
            fprintf(stderr, "Error muxing packet\n");
            break;
        }
        av_packet_unref(&pkt);
    }

    av_write_trailer(ofmt_ctx.Raw());
end:

    /* close output */
    if (ofmt_ctx.Raw() && !(ofmt->flags & AVFMT_NOFILE))
        avio_closep(&ofmt_ctx.Raw()->pb);

    if (ret < 0 && ret != AVERROR_EOF) {
        fprintf(stderr, "Error occurred: %s\n", ret);
        return 1;
    }

    return 0;
}
