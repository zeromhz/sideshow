#include "video_decoder.h"
#include "video_playback.h"
#include "log.h"
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>
#include <libavutil/display.h>
#include <libavutil/log.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cerrno>
#include <fcntl.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>
namespace sideshow {
namespace {
void checked(int result, const char* operation) {
    if(result >= 0) return;
    char message[AV_ERROR_MAX_STRING_SIZE]; av_strerror(result,message,sizeof(message));
    throw std::runtime_error(std::string(operation)+": "+message);
}
int openLocal(const std::filesystem::path& path) {
    // Walk directory descriptors: a concurrent symlink replacement cannot redirect an open.
    auto absolute=std::filesystem::absolute(path);
    int parent=::open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC);
    if(parent<0) return -1;
    auto relative=absolute.relative_path();
    for(auto it=relative.begin();it!=relative.end();++it) {
        if(*it==".." || *it==".") {close(parent);errno=EACCES;return -1;}
        auto next=it;++next;
        const int flags=O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK|(next==relative.end()?0:O_DIRECTORY);
        int child=openat(parent,it->c_str(),flags);int error=errno;close(parent);
        if(child<0) {errno=error;return -1;}
        parent=child;
    }
    return parent;
}
constexpr int64_t kMaxSourcePixels = 8847360;
bool validSourceSize(int width, int height) {
    return width > 0 && height > 0 && width <= 4096 && height <= 4096 &&
           int64_t(width) * height <= kMaxSourcePixels;
}
int allocateVideoBuffer(AVCodecContext* codec, AVFrame* frame, int flags) {
    // This callback can run on decoder threads: no exceptions or shared state.
    if (!validSourceSize(frame->width, frame->height)) return AVERROR(EINVAL);
    return avcodec_default_get_buffer2(codec, frame, flags);
}
// Rotation from a stream's display matrix, in quarter turns (0, 90, 180, 270); -1 if absent.
// FFmpeg 6.1 (libavcodec 60.31) moved stream side data to codecpar->coded_side_data.
int streamRotation(const AVStream* s) {
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(60, 31, 100)
    auto side=av_packet_side_data_get(s->codecpar->coded_side_data,s->codecpar->nb_coded_side_data,AV_PKT_DATA_DISPLAYMATRIX);
    const uint8_t* matrix=side ? side->data : nullptr;
    const size_t matrixSize=side ? side->size : 0;
#else
    size_t matrixSize=0;
    const uint8_t* matrix=av_stream_get_side_data(s,AV_PKT_DATA_DISPLAYMATRIX,&matrixSize);
#endif
    if(!matrix || matrixSize<9*sizeof(int32_t)) return -1;
    const double angle=av_display_rotation_get(reinterpret_cast<const int32_t*>(matrix));
    if(!std::isfinite(angle)) return -1;
    return ((int(std::round(angle/90))*90)%360+360)%360;
}
bool hdr(AVColorTransferCharacteristic transfer) {
    return transfer == AVCOL_TRC_SMPTE2084 || transfer == AVCOL_TRC_ARIB_STD_B67;
}
}
std::pair<int,int> videoOutputSize(int w,int h) {
    if(!validSourceSize(w,h))
        throw std::runtime_error("Video dimensions exceed supported limits");
    const double scale=std::min({1.0, double(w>=h?1920:1080)/w, double(w>=h?1080:1920)/h});
    return {std::max(1,int(w*scale)),std::max(1,int(h*scale))};
}
struct VideoDecoder::Impl {
    std::function<bool()> cancelled;
    std::filesystem::path path;
    VideoDecoderOptions options;
    std::chrono::steady_clock::time_point deadline;
    int fd=-1, stream=-1;
    AVIOContext* io=nullptr;
    AVFormatContext* format=nullptr;
    AVCodecContext* codec=nullptr;
    AVPacket* packet=nullptr;
    AVFrame* frame=nullptr;
    SwsContext* scaler=nullptr;
    VideoTimestamps timestamps{30};
    bool packetPending=false, draining=false, eof=false;
    int rotation=0;
    // Hardware decoding (HEVC on the Pi 5): frames arrive as DRM_PRIME and are downloaded into
    // `downloaded` before the usual YUV path. `delivered` records whether this decoder has
    // produced a frame, so a hardware failure before that can fall back to software.
    AVBufferRef* hardwareDevice=nullptr;
    AVPixelFormat hardwareFormat=AV_PIX_FMT_NONE;
    AVFrame* downloaded=nullptr;
    bool hardware=false, delivered=false;
    ~Impl() { release(); }
    void release() {
        sws_freeContext(scaler); scaler=nullptr; av_frame_free(&frame); av_frame_free(&downloaded); av_packet_free(&packet);
        avcodec_free_context(&codec); avformat_close_input(&format); av_buffer_unref(&hardwareDevice);
        if(io) { av_freep(&io->buffer); avio_context_free(&io); }
        if(fd>=0) close(fd);
        fd=-1; stream=-1; timestamps=VideoTimestamps(30); packetPending=draining=eof=false; rotation=0;
        hardwareFormat=AV_PIX_FMT_NONE; hardware=false;
    }
    static AVPixelFormat pickFormat(AVCodecContext* context, const AVPixelFormat* formats) {
        const auto& self=*static_cast<const Impl*>(context->opaque);
        for(auto f=formats;*f!=AV_PIX_FMT_NONE;++f) if(*f==self.hardwareFormat) return *f;
        // The hardware format was not offered for this stream: take the first software format.
        for(auto f=formats;*f!=AV_PIX_FMT_NONE;++f) {
            const auto* d=av_pix_fmt_desc_get(*f);
            if(d && !(d->flags & AV_PIX_FMT_FLAG_HWACCEL)) return *f;
        }
        return AV_PIX_FMT_NONE;
    }
    // Uses the decoder's DRM hardware configuration when FFmpeg offers one (Raspberry Pi OS's
    // FFmpeg exposes the Pi 5 HEVC block this way). Builds without it decode in software.
    void setupHardware(const AVCodec* decoder) {
        for(int i=0;;++i) {
            const AVCodecHWConfig* config=avcodec_get_hw_config(decoder,i);
            if(!config) return;
            if(!(config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) || config->device_type!=AV_HWDEVICE_TYPE_DRM) continue;
            if(av_hwdevice_ctx_create(&hardwareDevice,AV_HWDEVICE_TYPE_DRM,nullptr,nullptr,0)<0) return;
            codec->hw_device_ctx=av_buffer_ref(hardwareDevice);
            if(!codec->hw_device_ctx) throw std::bad_alloc();
            hardwareFormat=config->pix_fmt;codec->opaque=this;codec->get_format=pickFormat;hardware=true;
            return;
        }
    }
    bool interrupted() const { return (cancelled && cancelled()) || std::chrono::steady_clock::now()>=deadline; }
    void check() const { if(interrupted()) throw std::runtime_error("Video operation cancelled or timed out"); }
    void start() { deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5); check(); }
    static int interrupt(void* opaque) { return static_cast<Impl*>(opaque)->interrupted(); }
    static int read(void* opaque,uint8_t* data,int size) {
        auto& self=*static_cast<Impl*>(opaque);
        if(self.interrupted()) return AVERROR_EXIT;
        auto result=::read(self.fd,data,size);
        return result>0 ? int(result) : result==0 ? AVERROR_EOF : AVERROR(errno);
    }
    static int64_t seek(void* opaque,int64_t offset,int whence) {
        auto& self=*static_cast<Impl*>(opaque);
        if(self.interrupted()) return AVERROR_EXIT;
        if(whence==AVSEEK_SIZE) { struct stat st{}; return fstat(self.fd,&st)==0 ? st.st_size : AVERROR(errno); }
        auto result=lseek(self.fd,offset,whence & ~AVSEEK_FORCE);
        return result<0 ? AVERROR(errno) : result;
    }
    static int denyOpen(AVFormatContext*,AVIOContext**,const char*,int,AVDictionary**) { return AVERROR(EACCES); }
    void open(bool allowHardware) {
        start();
        fd=openLocal(path);
        struct stat st{};
        if(fd<0 || fstat(fd,&st)!=0 || !S_ISREG(st.st_mode)) throw std::runtime_error("Video input is not an accessible regular file");
        auto buffer=static_cast<unsigned char*>(av_malloc(32768));
        if(!buffer) throw std::bad_alloc();
        io=avio_alloc_context(buffer,32768,0,this,read,nullptr,seek);
        if(!io) {av_free(buffer);throw std::bad_alloc();}
        format=avformat_alloc_context(); if(!format) throw std::bad_alloc();
        format->pb=io; format->flags|=AVFMT_FLAG_CUSTOM_IO;
        format->interrupt_callback={interrupt,this}; format->io_open=denyOpen;
        format->probesize=5*1024*1024; format->max_analyze_duration=5*AV_TIME_BASE;
        checked(avformat_open_input(&format,nullptr,nullptr,nullptr),"Open video");
        // Read rotation now: with probe decoders disabled (below), FFmpeg 6.1's
        // avformat_find_stream_info writes the stream parameters back without their side data.
        std::vector<int> rotations;
        for(unsigned i=0;i<format->nb_streams;++i) rotations.push_back(streamRotation(format->streams[i]));
        // Probe headers/parsers only. Internal probe decoders cannot use our guarded
        // allocator, and could otherwise allocate oversized video (or audio) frames.
        format->codec_whitelist = av_strdup("");
        if (!format->codec_whitelist) throw std::bad_alloc();
        // With codecs disabled, FFmpeg logs "Codec ... not on whitelist" errors while probing.
        // They are expected; silence FFmpeg for this call only.
        start();
        const int logLevel=av_log_get_level();av_log_set_level(AV_LOG_FATAL);
        const int probed=avformat_find_stream_info(format,nullptr);
        av_log_set_level(logLevel);
        checked(probed,"Probe video");
        for(unsigned i=0;i<format->nb_streams;++i) {
            auto s=format->streams[i];
            if(s->codecpar->codec_type==AVMEDIA_TYPE_VIDEO && !(s->disposition & AV_DISPOSITION_ATTACHED_PIC)) {stream=int(i);break;}
        }
        if(stream<0) throw std::runtime_error("No video stream in file");
        auto s=format->streams[stream]; auto params=s->codecpar;
        videoOutputSize(params->width,params->height);
        if(hdr(params->color_trc)) throw std::runtime_error("HDR video is unsupported");
        if(params->field_order!=AV_FIELD_UNKNOWN && params->field_order!=AV_FIELD_PROGRESSIVE)
            throw std::runtime_error("Interlaced video is unsupported");
        const auto decoder=avcodec_find_decoder(params->codec_id);
        if(!decoder) throw std::runtime_error("Unsupported video codec");
        codec=avcodec_alloc_context3(decoder); if(!codec) throw std::bad_alloc();
        checked(avcodec_parameters_to_context(codec,params),"Configure codec");
        codec->max_pixels = kMaxSourcePixels;
        codec->get_buffer2 = allocateVideoBuffer;
        if(allowHardware && params->codec_id==AV_CODEC_ID_HEVC) setupHardware(decoder);
#if LIBAVCODEC_VERSION_MAJOR < 60
        codec->thread_safe_callbacks = 1;
#endif
        // Software: three decode threads leave a core of the Pi 5's four for rendering and audio.
        // Hardware does the decoding itself, so one thread is enough.
        codec->thread_count=hardware ? 1 : 3; codec->thread_type=FF_THREAD_FRAME|FF_THREAD_SLICE;
        checked(avcodec_open2(codec,decoder,nullptr),"Open codec");
        packet=av_packet_alloc(); frame=av_frame_alloc(); downloaded=av_frame_alloc();
        if(!packet || !frame || !downloaded) throw std::bad_alloc();
        if(params->codec_id==AV_CODEC_ID_HEVC && allowHardware)
            logInfo(std::string("video: ")+(hardware ? "hardware" : "software")+" HEVC decoding for "+path.filename().string());
        auto rate=av_guess_frame_rate(format,s,nullptr); timestamps=VideoTimestamps(av_q2d(rate));
        rotation=std::max(0,stream<int(rotations.size()) && rotations[stream]>=0 ? rotations[stream] : streamRotation(s));
    }
    VideoFrame convert() {
        check();
        AVFrame* f=frame;
        if(hardware && frame->format==hardwareFormat) {
            // Copy the hardware frame into ordinary memory (YUV); the rest is the software path.
            av_frame_unref(downloaded);
            // Prefer a layout the rest of the path handles directly (planar 4:2:0, then NV12)
            // over the decoder's native one (the Pi's is Broadcom's column-tiled SAND layout).
            AVPixelFormat* offered=nullptr;
            if(frame->hw_frames_ctx && av_hwframe_transfer_get_formats(frame->hw_frames_ctx,AV_HWFRAME_TRANSFER_DIRECTION_FROM,&offered,0)>=0 && offered) {
                AVPixelFormat pick=offered[0];
                for(auto o=offered;*o!=AV_PIX_FMT_NONE;++o) if(*o==AV_PIX_FMT_YUV420P) {pick=*o;break;}
                if(pick!=AV_PIX_FMT_YUV420P) for(auto o=offered;*o!=AV_PIX_FMT_NONE;++o) if(*o==AV_PIX_FMT_NV12) {pick=*o;break;}
                av_free(offered);
                downloaded->format=pick;
            }
            checked(av_hwframe_transfer_data(downloaded,frame,0),"Download hardware video frame");
            checked(av_frame_copy_props(downloaded,frame),"Copy video frame properties");
            f=downloaded;
        }
        auto size=videoOutputSize(f->width,f->height);
#if LIBAVUTIL_VERSION_MAJOR >= 58
        const bool interlaced=f->flags & AV_FRAME_FLAG_INTERLACED;
#else
        const bool interlaced=f->interlaced_frame;
#endif
        if(interlaced) throw std::runtime_error("Interlaced video is unsupported");
        if(hdr(f->color_trc)) throw std::runtime_error("HDR video is unsupported");
        VideoFrame result; result.width=size.first;result.height=size.second;
        result.format=VideoPixels::Yuv420;result.stride=result.width;result.chromaStride=(result.width+1)/2;
        result.rotationDegrees=rotation;
        auto sar=av_guess_sample_aspect_ratio(format,format->streams[stream],f);
        result.pixelAspect=sar.num>0 && sar.den>0 ? av_q2d(sar) : 1;
        result.pixelAspect*=double(f->width)*result.height/(double(f->height)*result.width);
        const double base=av_q2d(format->streams[stream]->time_base);
#if LIBAVUTIL_VERSION_MAJOR >= 58
        auto duration=f->duration;
#else
        auto duration=f->pkt_duration;
#endif
        auto times=timestamps.normalize(f->best_effort_timestamp==AV_NOPTS_VALUE ? std::nullopt : std::optional<double>(f->best_effort_timestamp*base), duration>0?std::optional<double>(duration*base):std::nullopt);
        result.pts=times.first;result.duration=times.second;
        // Frames go to the GPU as 8-bit planar YUV 4:2:0, converted to RGB in the shader. The
        // common case (8-bit 4:2:0 already within the output box) is a plane copy; anything else
        // (other formats, more bits, or footage to shrink) goes through swscale to YUV 4:2:0,
        // keeping its colour matrix and range.
        const auto source=static_cast<AVPixelFormat>(f->format);
        const AVPixFmtDescriptor* descriptor=av_pix_fmt_desc_get(source);
        if(!descriptor) throw std::runtime_error("Unknown video pixel format");
        const bool rgbSource=descriptor->flags & AV_PIX_FMT_FLAG_RGB;
        const int matrix=f->colorspace==AVCOL_SPC_BT709 ? SWS_CS_ITU709 :
            f->colorspace==AVCOL_SPC_BT2020_NCL ? SWS_CS_BT2020 :
            f->colorspace==AVCOL_SPC_UNSPECIFIED && f->height>=720 ? SWS_CS_ITU709 : SWS_CS_ITU601;
        result.matrix=matrix==SWS_CS_ITU709 ? VideoMatrix::Bt709 : matrix==SWS_CS_BT2020 ? VideoMatrix::Bt2020 : VideoMatrix::Bt601;
        result.fullRange=!rgbSource && (f->color_range==AVCOL_RANGE_JPEG || source==AV_PIX_FMT_YUVJ420P);
        const int chromaHeight=(result.height+1)/2;
        const size_t lumaBytes=size_t(result.stride)*result.height, chromaBytes=size_t(result.chromaStride)*chromaHeight;
        result.pixels.resize(lumaBytes+2*chromaBytes);
        uint8_t* planes[]={result.pixels.data(),result.pixels.data()+lumaBytes,result.pixels.data()+lumaBytes+chromaBytes,nullptr};
        int strides[]={result.stride,result.chromaStride,result.chromaStride,0};
        if((source==AV_PIX_FMT_YUV420P || source==AV_PIX_FMT_YUVJ420P) && f->width==result.width && f->height==result.height) {
            av_image_copy_plane(planes[0],strides[0],f->data[0],f->linesize[0],result.width,result.height);
            av_image_copy_plane(planes[1],strides[1],f->data[1],f->linesize[1],result.chromaStride,chromaHeight);
            av_image_copy_plane(planes[2],strides[2],f->data[2],f->linesize[2],result.chromaStride,chromaHeight);
            return result;
        }
        scaler=sws_getCachedContext(scaler,f->width,f->height,source,result.width,result.height,AV_PIX_FMT_YUV420P,SWS_BILINEAR,nullptr,nullptr,nullptr);
        if(!scaler) throw std::runtime_error("Video color converter allocation failed");
        const auto coefficients=sws_getCoefficients(matrix);
        checked(sws_setColorspaceDetails(scaler,coefficients,rgbSource ? 1 : int(result.fullRange),coefficients,result.fullRange,0,1<<16,1<<16),"Video colorspace");
        checked(sws_scale(scaler,f->data,f->linesize,0,f->height,planes,strides),"Convert video");
        return result;
    }
    // If hardware decoding fails (or ends) before its first frame, reopen the clip in software.
    std::optional<VideoFrame> next() {
        try {
            auto result=decode();
            if(result || !hardware || delivered) {if(result) delivered=true;return result;}
            logWarn("video: hardware HEVC decoding produced no frames; using software for "+path.filename().string());
        } catch(const std::exception& e) {
            if(!hardware || delivered || interrupted()) throw;
            logWarn(std::string("video: hardware HEVC decoding failed (")+e.what()+"); using software for "+path.filename().string());
        }
        release();open(false);
        return next();
    }
    std::optional<VideoFrame> decode() {
        start(); if(eof) return std::nullopt;
        for(;;) {
            check();
            int result=avcodec_receive_frame(codec,frame);
            if(result==0) {auto output=convert();av_frame_unref(frame);return output;}
            if(result==AVERROR_EOF) {eof=true;return std::nullopt;}
            if(result!=AVERROR(EAGAIN)) checked(result,"Decode video");
            if(draining) throw std::runtime_error("Video decoder did not drain");
            if(!packetPending) {
                do {check();result=av_read_frame(format,packet);
                    if(result<0) break;
                    if(packet->stream_index!=stream) av_packet_unref(packet);
                    else {packetPending=true;break;}
                } while(true);
                if(result==AVERROR_EOF) {
                    checked(avcodec_send_packet(codec,nullptr),"Drain video");draining=true;continue;
                }
                checked(result,"Read video packet");
            }
            result=avcodec_send_packet(codec,packet);
            if(result==AVERROR(EAGAIN)) continue;
            checked(result,"Submit video packet");av_packet_unref(packet);packetPending=false;
        }
    }
};
VideoDecoder::VideoDecoder(const std::filesystem::path& file,std::function<bool()> cancelled,VideoDecoderOptions options) : impl_(std::make_unique<Impl>()) {
    impl_->cancelled=std::move(cancelled);impl_->path=file;impl_->options=options;impl_->open(options.hardware);
}
bool VideoDecoder::hardwareActive() const { return impl_->hardware; }
VideoDecoder::~VideoDecoder()=default;
std::optional<VideoFrame> VideoDecoder::next() { return impl_->next(); }
void VideoDecoder::rewind() {
    auto& p=*impl_;p.start(); auto stream=p.format->streams[p.stream];
    checked(av_seek_frame(p.format,p.stream,stream->start_time==AV_NOPTS_VALUE?0:stream->start_time,AVSEEK_FLAG_BACKWARD),"Rewind video");
    avcodec_flush_buffers(p.codec);av_packet_unref(p.packet);av_frame_unref(p.frame);av_frame_unref(p.downloaded);
    p.timestamps.reset();p.packetPending=false;p.draining=false;p.eof=false;
}
}
