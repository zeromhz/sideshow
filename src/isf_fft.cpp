#include "isf_fft.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <stdexcept>
namespace sideshow {
std::vector<float> isfMagnitudeSpectrum(const std::vector<float>& mono,size_t bins){
 if(!bins||bins>4096)throw std::invalid_argument("ISF FFT width outside 1..4096");
 size_t size=2;while(size<2*bins)size*=2;
 constexpr double pi=3.14159265358979323846;
 std::vector<std::complex<double>> a(size);double sum=0;
 // Periodic Hann avoids an all-zero window when N=2.
 for(size_t n=0;n<size;++n){double w=.5-.5*std::cos(2*pi*n/size);sum+=w;double x=n+mono.size()>=size?mono[n+mono.size()-size]:0;if(!std::isfinite(x))x=0;a[n]=std::clamp(x,-1.0,1.0)*w;}
 for(size_t i=1,j=0;i<size;++i){size_t bit=size>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;if(i<j)std::swap(a[i],a[j]);}
 for(size_t length=2;length<=size;length*=2){auto step=std::polar(1.0,-2*pi/length);for(size_t start=0;start<size;start+=length){std::complex<double> w=1;for(size_t j=0;j<length/2;++j){auto u=a[start+j],v=a[start+j+length/2]*w;a[start+j]=u+v;a[start+j+length/2]=u-v;w*=step;}}}
 std::vector<float> result(bins);size_t available=size/2;
 for(size_t b=0;b<bins;++b){size_t begin=b*available/bins,end=(b+1)*available/bins;double total=0;for(size_t n=begin;n<end;++n)total+=std::abs(a[n])*(n?2:1)/sum;result[b]=float(total/(end-begin));}return result;
}
}
