#pragma once
#include "command.h"
namespace sideshow {
class IsfControl {
public:
 virtual ~IsfControl()=default;
 virtual void setParameter(const cmd::IsfParam&)=0;
 virtual void triggerEvent(const cmd::IsfEvent&)=0;
};
}
