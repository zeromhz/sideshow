#include "video_decoder.h"
#include "video_playback.h"
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>
#include <libavutil/display.h>
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
    ~Impl() {
        sws_freeContext(scaler); av_frame_free(&frame); av_packet_free(&packet);
        avcodec_free_context(&codec); avformat_close_input(&format);
        if(io) { av_freep(&io->buffer); avio_context_free(&io); }
        if(fd>=0) close(fd);
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
    void open(const std::filesystem::path& path) {
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
        // Probe headers/parsers only. Internal probe decoders cannot use our guarded
        // allocator, and could otherwise allocate oversized video (or audio) frames.
        format->codec_whitelist = av_strdup("");
        if (!format->codec_whitelist) throw std::bad_alloc();
        start(); checked(avformat_find_stream_info(format,nullptr),"Probe video");
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
#if LIBAVCODEC_VERSION_MAJOR < 60
        codec->thread_safe_callbacks = 1;
#endif
        // Three decode threads leave a core of the Pi 5's four for rendering and audio.
        codec->thread_count=3; codec->thread_type=FF_THREAD_FRAME|FF_THREAD_SLICE;
        checked(avcodec_open2(codec,decoder,nullptr),"Open codec");
        packet=av_packet_alloc(); frame=av_frame_alloc(); if(!packet || !frame) throw std::bad_alloc();
        auto rate=av_guess_frame_rate(format,s,nullptr); timestamps=VideoTimestamps(av_q2d(rate));
#if LIBAVFORMAT_VERSION_MAJOR >= 61
        auto side=av_packet_side_data_get(params->coded_side_data,params->nb_coded_side_data,AV_PKT_DATA_DISPLAYMATRIX);
        const auto matrix=side ? side->data : nullptr;
        const auto matrixSize=side ? side->size : 0;
#else
        size_t matrixSize=0;
        const auto matrix=av_stream_get_side_data(s,AV_PKT_DATA_DISPLAYMATRIX,&matrixSize);
#endif
        if(matrix && matrixSize>=9*sizeof(int32_t)) {
            double angle=av_display_rotation_get(reinterpret_cast<const int32_t*>(matrix));
            if(std::isfinite(angle)) rotation=((int(std::round(angle/90))*90)%360+360)%360;
        }
    }
    VideoFrame convert() {
        check();
        auto size=videoOutputSize(frame->width,frame->height);
#if LIBAVUTIL_VERSION_MAJOR >= 58
        const bool interlaced=frame->flags & AV_FRAME_FLAG_INTERLACED;
#else
        const bool interlaced=frame->interlaced_frame;
#endif
        if(interlaced) throw std::runtime_error("Interlaced video is unsupported");
        if(hdr(frame->color_trc)) throw std::runtime_error("HDR video is unsupported");
        VideoFrame result; result.width=size.first;result.height=size.second;result.stride=result.width*4;
        result.rotationDegrees=rotation;
        auto sar=av_guess_sample_aspect_ratio(format,format->streams[stream],frame);
        result.pixelAspect=sar.num>0 && sar.den>0 ? av_q2d(sar) : 1;
        result.pixelAspect*=double(frame->width)*result.height/(double(frame->height)*result.width);
        const double base=av_q2d(format->streams[stream]->time_base);
#if LIBAVUTIL_VERSION_MAJOR >= 58
        auto duration=frame->duration;
#else
        auto duration=frame->pkt_duration;
#endif
        auto times=timestamps.normalize(frame->best_effort_timestamp==AV_NOPTS_VALUE ? std::nullopt : std::optional<double>(frame->best_effort_timestamp*base), duration>0?std::optional<double>(duration*base):std::nullopt);
        result.pts=times.first;result.duration=times.second;
        scaler=sws_getCachedContext(scaler,frame->width,frame->height,static_cast<AVPixelFormat>(frame->format),result.width,result.height,AV_PIX_FMT_RGBA,SWS_BILINEAR,nullptr,nullptr,nullptr);
        if(!scaler) throw std::runtime_error("Video color converter allocation failed");
        int matrix=frame->colorspace==AVCOL_SPC_BT709 ? SWS_CS_ITU709 :
            frame->colorspace==AVCOL_SPC_BT2020_NCL ? SWS_CS_BT2020 :
            frame->colorspace==AVCOL_SPC_UNSPECIFIED && frame->height>=720 ? SWS_CS_ITU709 : SWS_CS_ITU601;
        const auto coefficients=sws_getCoefficients(matrix);
        checked(sws_setColorspaceDetails(scaler,coefficients,frame->color_range==AVCOL_RANGE_JPEG,coefficients,1,0,1<<16,1<<16),"Video colorspace");
        result.rgba.resize(size_t(result.stride)*result.height);
        uint8_t* output[]={result.rgba.data(),nullptr,nullptr,nullptr}; int strides[]={result.stride,0,0,0};
        checked(sws_scale(scaler,frame->data,frame->linesize,0,frame->height,output,strides),"Convert video");
        // swscale already writes opaque alpha for sources without an alpha channel; only sources
        // that carry their own alpha need flattening.
        const AVPixFmtDescriptor* descriptor=av_pix_fmt_desc_get(static_cast<AVPixelFormat>(frame->format));
        if(!descriptor || (descriptor->flags & AV_PIX_FMT_FLAG_ALPHA))
            for(size_t i=3;i<result.rgba.size();i+=4) result.rgba[i]=255;
        return result;
    }
    std::optional<VideoFrame> next() {
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
VideoDecoder::VideoDecoder(const std::filesystem::path& file,std::function<bool()> cancelled) : impl_(std::make_unique<Impl>()) {
    impl_->cancelled=std::move(cancelled);impl_->open(file);
}
VideoDecoder::~VideoDecoder()=default;
std::optional<VideoFrame> VideoDecoder::next() { return impl_->next(); }
void VideoDecoder::rewind() {
    auto& p=*impl_;p.start(); auto stream=p.format->streams[p.stream];
    checked(av_seek_frame(p.format,p.stream,stream->start_time==AV_NOPTS_VALUE?0:stream->start_time,AVSEEK_FLAG_BACKWARD),"Rewind video");
    avcodec_flush_buffers(p.codec);av_packet_unref(p.packet);av_frame_unref(p.frame);
    p.timestamps.reset();p.packetPending=false;p.draining=false;p.eof=false;
}
}
