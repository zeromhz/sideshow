#include "support/isf_gl_context.h"
#include "support/isf_tree.h"
#include "isf_backend.h"
#include "isf_services.h"
#include "gl.h"
#include <iostream>
#include <cmath>
#include <string>
#include <thread>
#include <utility>
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
 IsfBackend backend(init,services.loader(),services.context(),services.budget());
 requireIsf(backend.setParameter({"level",{.4f}}).status=="accepted","queued control claimed applied");ready(backend);
 requireIsf(backend.setParameter({"missing",{.4f}}).status=="rejected","unknown parameter acknowledged");
 requireIsf(backend.setParameter({"level",{.4f,.5f}}).status=="rejected","wrong parameter arity acknowledged");
 requireIsf(backend.triggerEvent({"level"}).status=="rejected","non-event acknowledged");
 requireIsf(backend.setParameter({"level",{.4f}}).status=="accepted","control claimed presentation before rendering");backend.triggerEvent({"reset"});backend.render(0);requireIsf(red()>=177&&red()<=180,"event after readiness not presented");backend.render(.1f);requireIsf(red()>=100&&red()<=104,"queued parameter missing or event repeated");
 backend.selectPreset("b.fs");requireIsf(!backend.selectPreset("missing.fs"),"invalid newest selection accepted");for(int i=0;i<30;++i){backend.maintenance();backend.render(.01f);std::this_thread::sleep_for(std::chrono::milliseconds(2));}requireIsf(backend.selection().preset=="a.fs"&&red()<110,"stale candidate activated");
 backend.setParameter({"level",{.8f}});backend.triggerEvent({"reset"});backend.render(.01f);requireIsf(red()>=100&&red()<=104,"invalid newest preset routed controls to old shader");
 backend.selectPreset("b.fs");backend.selectPlaylist("missing");backend.setParameter({"level",{.9f}});backend.triggerEvent({"reset"});backend.render(.01f);requireIsf(red()>=100&&red()<=104,"invalid newest folder routed controls to old shader");
 backend.selectPreset("bad.fs");for(int i=0;i<30;++i){backend.maintenance();backend.render(.01f);std::this_thread::sleep_for(std::chrono::milliseconds(2));}requireIsf(backend.selection().preset=="a.fs","failed shader replaced active selection");
 backend.selectPreset("a.fs");for(int i=0;i<50;++i){backend.maintenance();backend.render(.01f);std::this_thread::sleep_for(std::chrono::milliseconds(2));}requireIsf(red()>=50&&red()<=52,"same-ID load did not restore defaults");backend.resize(180,320);backend.render(0);backend.resize(320,180);backend.render(0);
 auto settings=init.playback;settings.locked=false;backend.setSettings(settings);backend.render(2);
 for(int n=0;n<100&&backend.selection().preset!="b.fs";++n){backend.maintenance();backend.render(.001f);std::this_thread::sleep_for(std::chrono::milliseconds(2));}requireIsf(backend.selection().preset=="b.fs","duration did not advance");
 }
 {
 IsfBackend queued(init,services.loader(),services.context(),services.budget());for(int n=0;n<64;++n)queued.setParameter({"level",{float(n)/100}});requireIsf(queued.setParameter({"level",{.9f}}).status=="rejected","full control queue acknowledged");ready(queued);queued.render(0);requireIsf(red()>=159&&red()<=162,"pending control queue limit not enforced");
 }
 for(bool prepared : {false,true}) {
 IsfBackend recovery(init,services.loader(),services.context(),services.budget());
 ready(recovery);
 if(!prepared) recovery.render(0);
 recovery.setParameter({"level",{.4f}});
 recovery.triggerEvent({"reset"});
 isf_gl_failure::failNextCheck=true;
 recovery.render(.01f);
 requireIsf(!isf_gl_failure::failNextCheck,"allocation failure was not injected");
 requireIsf(red()>=50&&red()<=52,"rejected frame changed retained picture");
 recovery.render(.01f);
 requireIsf(red()>=177&&red()<=180,"rejected frame discarded pending event");
 recovery.render(.01f);
 requireIsf(red()>=50&&red()<=52,"event repeated or rejected parameter was retained");
 }
 {
 IsfBackend queuedEvent(init,services.loader(),services.context(),services.budget());
 queuedEvent.triggerEvent({"reset"});
 ready(queuedEvent);
 queuedEvent.clearAudioHistory(); // Force a redraw of the prepared first frame.
 isf_gl_failure::failNextCheck=true;
 queuedEvent.render(0);
 requireIsf(!isf_gl_failure::failNextCheck,"prepared redraw failure was not injected");
 requireIsf(red()>=177&&red()<=180,"prepared event was not presented");
 queuedEvent.render(.01f);
 requireIsf(red()>=50&&red()<=52,"already presented event repeated after rejection");
 }
 {
 // Render scale: the shader reports its own RENDERSIZE as colour (red = width/320, green =
 // height/320). The 320x180 window is under the 1280x720 cap, so only the scale shrinks it.
 IsfTree sized;
 const char* body="void main(){gl_FragColor=vec4(RENDERSIZE.x/320.0,RENDERSIZE.y/320.0,0,1);}";
 sized.file("full.fs",std::string("/*{}*/")+body);
 sized.file("half.fs",std::string(R"(/*{"SIDESHOW_RENDER_SCALE":0.5}*/)")+body);
 Config scaled;scaled.isf.root=sized.root.string();scaled.isf.scale["full.fs"]=0.25;
 IsfServices sizedServices(scaled.isf);
 auto sizeOf=[&](const Config& c,const char* id){
  BackendInit i{c,PlaybackSettings{1,false,true,false,1},{".",id},320,180};
  IsfBackend b(i,sizedServices.loader(),sizedServices.context(),sizedServices.budget());
  ready(b);b.render(0);b.render(.01f);
  unsigned char p[4];glReadPixels(160,90,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p);
  return std::pair<int,int>{int(p[0]*320/255.0+.5),int(p[1]*320/255.0+.5)};};
 Config plain=scaled;plain.isf.scale.clear();
 auto full=sizeOf(plain,"full.fs"),half=sizeOf(plain,"half.fs"),overridden=sizeOf(scaled,"full.fs");
 requireIsf(std::abs(full.first-320)<=1&&std::abs(full.second-180)<=1,"unscaled shader not at full size");
 requireIsf(std::abs(half.first-160)<=1&&std::abs(half.second-90)<=1,"SIDESHOW_RENDER_SCALE not applied");
 requireIsf(std::abs(overridden.first-80)<=1&&std::abs(overridden.second-45)<=1,"[isf.scale] override not applied");
 }
 requireIsf(services.budget().used()==0,"backend leaked textures");std::cout<<"ISF backend checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
