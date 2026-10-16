#pragma once
#include <array>
#include <vector>
namespace sideshow {
struct IsfAudioTexture {size_t width=0,channels=2;std::vector<float> samples;};
class IsfAudio {
public:
 void append(const float*,size_t frames);
 IsfAudioTexture waveform(size_t width) const;
 IsfAudioTexture spectrum(size_t width) const;
 void clear();
private:
 std::array<std::array<float,2>,8192> history_{};size_t next_=0;
};
}
