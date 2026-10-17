#pragma once
// Test-only wrapper around the real decoder allocator, including custom callbacks.
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
#include <atomic>
namespace video_decode_tracking {
inline std::atomic<unsigned> oversizedAllocations{0};
inline unsigned probeDecoderOpens = 0;
inline unsigned probeCalls = 0;
inline int (*allocate)(AVCodecContext*, AVFrame*, int) = nullptr;
inline int trackedAllocate(AVCodecContext* codec, AVFrame* frame, int flags) {
    const int result = allocate(codec, frame, flags);
    if (result >= 0 && (frame->width > 4096 || frame->height > 4096 ||
                       int64_t(frame->width) * frame->height > 8847360))
        ++oversizedAllocations;
    return result;
}
inline int open(AVCodecContext* context, const AVCodec* codec, AVDictionary** options) {
    allocate = context->get_buffer2;
    context->get_buffer2 = trackedAllocate;
    return ::avcodec_open2(context, codec, options);
}
inline int probe(AVFormatContext* context, AVDictionary** options) {
    ++probeCalls;
    // Internal libavformat calls bypass our macro wrapper. Exercise its decoder
    // whitelist through the real codec API before delegating the actual probe.
    // This tests the boundary policy without asserting a particular string.
    auto* decoder = avcodec_find_decoder(AV_CODEC_ID_H264);
    auto* candidate = avcodec_alloc_context3(decoder);
    if (!candidate) return AVERROR(ENOMEM);
    if (context->codec_whitelist) {
        candidate->codec_whitelist = av_strdup(context->codec_whitelist);
        if (!candidate->codec_whitelist) {
            avcodec_free_context(&candidate);
            return AVERROR(ENOMEM);
        }
    }
    if (::avcodec_open2(candidate, decoder, nullptr) >= 0) ++probeDecoderOpens;
    avcodec_free_context(&candidate);
    return ::avformat_find_stream_info(context, options);
}
}
#define avformat_find_stream_info video_decode_tracking::probe
#define avcodec_open2 video_decode_tracking::open
