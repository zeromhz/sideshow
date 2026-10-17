#pragma once
#include "command.h"
namespace sideshow {
class IsfControl {
public:
 virtual ~IsfControl()=default;
 virtual CommandResult setParameter(const cmd::IsfParam&)=0;
 virtual CommandResult triggerEvent(const cmd::IsfEvent&)=0;
};
}
