#include "isf_audio.h"
#include "isf_fft.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace sideshow {
void IsfAudio::append(const float* stereo,size_t frames){if(!stereo)return;for(size_t n=0;n<frames;++n){for(size_t c=0;c<2;++c){auto x=stereo[2*n+c];history_[next_][c]=std::isfinite(x)?std::clamp(x,-1.0f,1.0f):0;}next_=(next_+1)%history_.size();}}
void IsfAudio::clear(){history_={};next_=0;}
IsfAudioTexture IsfAudio::waveform(size_t width)const{
 if(!width||width>4096)throw std::invalid_argument("ISF waveform width outside 1..4096");IsfAudioTexture out{width,2,std::vector<float>(width*2)};
 for(size_t c=0;c<2;++c)for(size_t b=0;b<width;++b){size_t begin=b*8192/width,end=(b+1)*8192/width;double total=0;for(size_t n=begin;n<end;++n){double x=history_[(next_+n)%8192][c];total+=width==1?x*x:x;}double v=total/(end-begin);out.samples[c*width+b]=float(.5+.5*(width==1?std::sqrt(v):v));}return out;
}
IsfAudioTexture IsfAudio::spectrum(size_t width)const{
 if(!width||width>4096)throw std::invalid_argument("ISF spectrum width outside 1..4096");IsfAudioTexture out{width,2,{}};out.samples.reserve(width*2);std::vector<float> mono(8192);
 for(size_t c=0;c<2;++c){for(size_t n=0;n<8192;++n)mono[n]=history_[(next_+n)%8192][c];auto bins=isfMagnitudeSpectrum(mono,width);out.samples.insert(out.samples.end(),bins.begin(),bins.end());}return out;
}
}
