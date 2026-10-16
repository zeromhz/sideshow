#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "isf_audio.h"
#include "isf_fft.h"
#include <algorithm>
#include <cmath>
#include <limits>
using namespace sideshow;
TEST_CASE("ISF audio clears to midpoint and zero without stale history","[isf]") {
 IsfAudio a;for(auto w:{1,512,4096}){auto t=a.waveform(w);CHECK(t.samples.size()==size_t(w*2));CHECK(std::all_of(t.samples.begin(),t.samples.end(),[](float x){return x==0.5f;}));}
 std::vector<float> stereo(8192*2);for(size_t n=0;n<8192;++n){stereo[n*2]=1;stereo[n*2+1]=-1;}a.append(stereo.data(),8192);
 CHECK(a.waveform(512).samples[0]==1);CHECK(a.waveform(512).samples[512]==0);CHECK(a.waveform(1).samples[1]==1);
 a.clear();auto s=a.spectrum(512);CHECK(std::all_of(s.samples.begin(),s.samples.end(),[](float x){return x==0;}));CHECK(a.waveform(1).samples[0]==0.5f);
 CHECK_THROWS(a.waveform(0));CHECK_THROWS(a.spectrum(4097));
}
TEST_CASE("ISF FFT preserves frequency amplitude and independent channels","[isf]") {
 std::vector<float> sine(1024),stereo(2048);for(size_t n=0;n<1024;++n){sine[n]=std::sin(2*3.141592653589793*32*n/1024);stereo[2*n]=sine[n];}
 auto b=isfMagnitudeSpectrum(sine,512);CHECK(std::distance(b.begin(),std::max_element(b.begin(),b.end()))==32);CHECK(b[32]==Catch::Approx(1).margin(.02));
 IsfAudio a,c;a.append(stereo.data(),1024);c.append(stereo.data(),311);c.append(stereo.data()+622,713);CHECK(a.spectrum(512).samples==c.spectrum(512).samples);CHECK(a.waveform(77).samples==c.waveform(77).samples);
 auto spectrum=a.spectrum(512);CHECK(spectrum.samples[32]==Catch::Approx(1).margin(.02));CHECK(spectrum.samples[512+32]==0);
 CHECK(a.spectrum(333).samples.size()==666);
 float bad[2]={std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()};a.clear();a.append(bad,1);CHECK(a.waveform(1).samples[0]==.5f);
}
