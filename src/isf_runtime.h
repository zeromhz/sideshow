#pragma once
#include "isf_context.h"
#include "isf_budget.h"
#include "isf_images.h"
#include "isf_parameters.h"
#include "isf_audio.h"
#include <stdexcept>
namespace sideshow {
class IsfFrameRejected:public std::runtime_error {public:using std::runtime_error::runtime_error;};
class IsfRuntime {
public:
 IsfRuntime(IsfContext&,IsfBudget&,std::shared_ptr<const IsfSource>,int width,int height);
 ~IsfRuntime();
 const std::map<std::string,IsfValue>& defaults()const;
 bool update(const IsfParameters&,int width,int height,std::string& error,bool resetHistory=false);
 void draw(const IsfParameters&,const IsfAudio&,double time,double delta,uint64_t frameIndex);
 void present(int drawableWidth,int drawableHeight);
private:struct Impl;std::unique_ptr<Impl> impl_;
};
}
