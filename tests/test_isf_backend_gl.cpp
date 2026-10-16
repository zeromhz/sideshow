#include "support/isf_gl_context.h"
#include "support/isf_tree.h"
#include "isf_backend.h"
#include "isf_services.h"
#include "gl.h"
#include <iostream>
#include <thread>
using namespace sideshow;
static void ready(IsfBackend& b){for(int n=0;n<400;++n){b.maintenance();auto p=b.preparation();if(p.state==PreparationState::Ready)return;if(p.state==PreparationState::Failed)throw std::runtime_error(p.error);std::this_thread::sleep_for(std::chrono::milliseconds(5));}throw std::runtime_error("ISF backend timed out");}
static int red(){unsigned char p[4];glReadPixels(160,90,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);return p[0];}
int main(){try{
 IsfTestContext window;IsfTree tree;
 tree.file("a.fs",R"(/*{"INPUTS":[{"NAME":"level","TYPE":"float","DEFAULT":0.2},{"NAME":"reset","TYPE":"event"}],"PASSES":[{"TARGET":"history","PERSISTENT":true},{}]}*/
void main(){if(PASSINDEX==0)gl_FragColor=vec4(reset?0.7:level,0,0,1);else gl_FragColor=IMG_THIS_PIXEL(history);})");
 tree.file("b.fs","/*{}*/void main(){gl_FragColor=vec4(1,0,0,1);}");tree.file("bad.fs","/*{}*/invalid shader");
 Config config;config.isf.root=tree.root.string();config.isf.preset="a.fs";IsfServices services(config.isf);
 BackendInit init{config,PlaybackSettings{1,false,true,false,1},{".","a.fs"},320,180};
 {
 IsfBackend backend(init,services.loader(),services.context(),services.budget());backend.setParameter({"level",{.4f}});ready(backend);backend.triggerEvent({"reset"});backend.render(0);requireIsf(red()>=177&&red()<=180,"event after readiness not presented");backend.render(.1f);requireIsf(red()>=100&&red()<=104,"queued parameter missing or event repeated");
 backend.selectPreset("b.fs");requireIsf(!backend.selectPreset("missing.fs"),"invalid newest selection accepted");for(int i=0;i<30;++i){backend.maintenance();backend.render(.01f);std::this_thread::sleep_for(std::chrono::milliseconds(2));}requireIsf(backend.selection().preset=="a.fs"&&red()<110,"stale candidate activated");
 backend.selectPreset("bad.fs");for(int i=0;i<30;++i){backend.maintenance();backend.render(.01f);std::this_thread::sleep_for(std::chrono::milliseconds(2));}requireIsf(backend.selection().preset=="a.fs","failed shader replaced active selection");
 backend.selectPreset("a.fs");for(int i=0;i<50;++i){backend.maintenance();backend.render(.01f);std::this_thread::sleep_for(std::chrono::milliseconds(2));}requireIsf(red()>=50&&red()<=52,"same-ID load did not restore defaults");backend.resize(180,320);backend.render(0);backend.resize(320,180);backend.render(0);
 auto settings=init.playback;settings.locked=false;backend.setSettings(settings);backend.render(2);
 for(int n=0;n<100&&backend.selection().preset!="b.fs";++n){backend.maintenance();backend.render(.001f);std::this_thread::sleep_for(std::chrono::milliseconds(2));}requireIsf(backend.selection().preset=="b.fs","duration did not advance");
 }
 {
 IsfBackend queued(init,services.loader(),services.context(),services.budget());for(int n=0;n<64;++n)queued.setParameter({"level",{float(n)/100}});queued.setParameter({"level",{.9f}});ready(queued);queued.render(0);requireIsf(red()>=159&&red()<=162,"pending control queue limit not enforced");
 }
 requireIsf(services.budget().used()==0,"backend leaked textures");std::cout<<"ISF backend checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
