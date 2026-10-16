#pragma once
#include "config.h"
#include "isf_loader.h"
#include "isf_context.h"
#include "isf_budget.h"
namespace sideshow {
class IsfServices {
public:
 explicit IsfServices(const Config::Isf& c):loader_(c.root){}
 IsfLoader& loader(){return loader_;}
 IsfContext& context(){if(!context_)context_=std::make_unique<IsfContext>();return *context_;}
 IsfBudget& budget(){return budget_;}
private:std::unique_ptr<IsfContext> context_;IsfBudget budget_;IsfLoader loader_;
};
}
