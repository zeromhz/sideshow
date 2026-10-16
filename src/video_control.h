#pragma once
#include "video_types.h"
namespace sideshow {
class VideoControl {
public:
    virtual ~VideoControl()=default;
    virtual void setEnd(VideoEnd)=0;
    virtual VideoEnd end() const=0;
    virtual void setPaused(bool)=0;
    virtual void restart()=0;
};
}
