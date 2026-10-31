#include "support/isf_gl_context.h"
#include "isf_runtime.h"
#include "fluid_gl.h"
#include "isf_document.h"
#include "gl.h"
#include <iostream>
#include <array>
#include <cmath>
#include <fstream>
#include <numeric>
using namespace sideshow;
static std::array<unsigned char,4> pixel(int x,int y){std::array<unsigned char,4> p{};glReadPixels(x,y,1,1,GL_RGBA,GL_UNSIGNED_BYTE,p.data());return p;}
static std::shared_ptr<IsfSource> source(const std::string& text){auto s=std::make_shared<IsfSource>();s->document=parseIsfDocument("test.fs",text,"");return s;}
int main(){try{
 IsfTestContext window;IsfContext context;IsfBudget budget;IsfAudio audio;
 {
 auto s=source("/*{\"ISFVSN\":\"2.0\"}*/\nvoid main(){gl_FragColor=vec4(1,0,0,0.5);}");
 IsfRuntime r(context,budget,s,32,32);IsfParameters p(s->document.inputs);p.initializeDefaults(r.defaults());
 r.draw(p,audio,0,0,0);r.present(320,180);
 auto center=pixel(160,90),bar=pixel(0,90);requireIsf(center[0]>=126&&center[0]<=129&&center[3]==255,"straight alpha presentation failed");requireIsf(bar[0]==0&&bar[3]==255,"aspect-fit bars failed");
 }
 requireIsf(budget.used()==0,"runtime destruction leaked reservations");
 {
 auto s=source(R"(/*{"ISFVSN":"2.0","INPUTS":[{"NAME":"reset","TYPE":"event"}],"PASSES":[{"TARGET":"history","PERSISTENT":true},{}]}*/
void main(){if(PASSINDEX==0)gl_FragColor=vec4(reset?0.7:IMG_THIS_PIXEL(history).r+0.1,0,0,1);else gl_FragColor=IMG_THIS_PIXEL(history);})");
 IsfRuntime r(context,budget,s,32,32);IsfParameters p(s->document.inputs);p.initializeDefaults(r.defaults());std::string error;
 p.trigger("reset",error);r.draw(p,audio,0,0,0);r.present(320,180);requireIsf(pixel(160,90)[0]>175,"first-frame event missing");p.presented();
 r.draw(p,audio,.1,.1,1);r.present(320,180);requireIsf(pixel(160,90)[0]>200,"feedback history missing");
 }
 requireIsf(budget.used()==0,"feedback leaked reservations");

 {
 auto s=source(R"(/*{"ISFVSN":"2.0","INPUTS":[{"NAME":"speed","TYPE":"float"},{"NAME":"size","TYPE":"float","DEFAULT":16}],"PASSES":[{"TARGET":"buffer","WIDTH":"$size","HEIGHT":"$HEIGHT/2","FLOAT":true},{}]}*/
void main(){if(PASSINDEX==0)gl_FragColor=vec4(speed,0,0,1);else gl_FragColor=vec4(IMG_THIS_PIXEL(buffer).r/7.0,TIME, float(FRAMEINDEX)/10.0,1);})");
 IsfRuntime r(context,budget,s,32,32);IsfParameters p(s->document.inputs);p.initializeDefaults(r.defaults());std::string err;
 requireIsf(p.set({"speed",{7.0f}},err),"unbounded parameter rejected");r.draw(p,audio,0,0,0);r.present(320,180);requireIsf(pixel(160,90)[0]>250,"FLOAT target lost precision or value clamped");
 auto invalid=p;invalid.set({"size",{5000.0f}},err);requireIsf(!r.update(invalid,32,32,err),"extreme expression accepted");r.present(320,180);requireIsf(pixel(160,90)[0]>250,"rejected update changed picture");
 r.draw(p,audio,.2,.2,2);r.present(320,180);auto timed=pixel(160,90);requireIsf(timed[1]>=50&&timed[1]<=52&&timed[2]>=50&&timed[2]<=52,"explicit frame timing wrong");
 auto before=budget.used();try{IsfRuntime bad(context,budget,source("/*{}*/\ninvalid GLSL"),32,32);IsfParameters empty({});empty.initializeDefaults({});bad.draw(empty,audio,0,0,0);throw std::runtime_error("bad shader accepted");}catch(const std::exception& e){requireIsf(std::string(e.what())!="bad shader accepted","bad shader accepted");}
 requireIsf(budget.used()==before,"failed candidate leaked budget");r.present(320,180);requireIsf(pixel(160,90)[0]>250,"failed candidate corrupted retained picture");
 }
 requireIsf(budget.used()==0,"dynamic runtime leaked budget");
 {
 auto s=source(R"(/*{"ISFVSN":"2.0","IMPORTED":{"picture":{"PATH":"pixel.png"}}}*/void main(){gl_FragColor=IMG_THIS_PIXEL(picture);})");
 s->images.push_back({"picture",1,1,{255,0,0,255}});
 s->document.vertex="void main(){isf_vertShaderInit();}";
 IsfRuntime r(context,budget,s,32,32);IsfParameters p({});p.initializeDefaults(r.defaults());
 GLuint sampler=0,pbo=0;glGenSamplers(1,&sampler);glSamplerParameteri(sampler,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);glBindSampler(0,sampler);
 glGenBuffers(1,&pbo);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,pbo);glBufferData(GL_PIXEL_UNPACK_BUFFER,64,nullptr,GL_STATIC_DRAW);glPixelStorei(GL_UNPACK_ROW_LENGTH,17);glPixelStorei(GL_UNPACK_SKIP_ROWS,3);glActiveTexture(GL_TEXTURE7);glEnable(GL_SCISSOR_TEST);glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
 r.draw(p,audio,0,0,0);r.present(320,180);requireIsf(pixel(160,90)[0]==255,"dirty state broke imported image");
 GLint active=0,binding=0,row=0;glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&binding);glGetIntegerv(GL_UNPACK_ROW_LENGTH,&row);requireIsf(active==GL_TEXTURE7&&binding==GLint(pbo)&&row==17&&glIsEnabled(GL_SCISSOR_TEST),"GL state not restored");
 glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glDeleteBuffers(1,&pbo);glBindSampler(0,0);glDeleteSamplers(1,&sampler);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);glPixelStorei(GL_UNPACK_SKIP_ROWS,0);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glDisable(GL_SCISSOR_TEST);glActiveTexture(GL_TEXTURE0);
 }
 requireIsf(budget.used()==0,"imported runtime leaked budget");

 {
 auto s=source(R"(/*{"ISFVSN":"2.0","INPUTS":[{"NAME":"fft","TYPE":"audioFFT","MAX":512}]}*/
void main(){gl_FragColor=vec4(IMG_PIXEL(fft,vec2(32.5,0.5)).r,0,0,1);})");
 IsfRuntime r(context,budget,s,32,32);IsfParameters p(s->document.inputs);p.initializeDefaults(r.defaults());IsfAudio captured;std::vector<float> wave(2048);for(int i=0;i<1024;++i)wave[2*i]=std::sin(2*3.141592653589793*32*i/1024);captured.append(wave.data(),1024);r.draw(p,captured,0,0,0);r.present(320,180);requireIsf(pixel(160,90)[0]>245,"audio FFT texture incorrect");captured.clear();r.draw(p,captured,.1,.1,1);r.present(320,180);requireIsf(pixel(160,90)[0]==0,"audio silence retained energy");
 }
 for(int i=0;i<10;++i){auto s=source("/*{}*/void main(){gl_FragColor=vec4(1);}");IsfRuntime r(context,budget,s,32,32);IsfParameters p({});p.initializeDefaults(r.defaults());r.draw(p,audio,0,0,0);}
 requireIsf(budget.used()==0,"repeated loads leaked budget");
 {IsfBudget tiny(100);bool rejected=false;try{IsfRuntime r(context,tiny,source("/*{}*/void main(){gl_FragColor=vec4(1);}"),32,32);}catch(const std::exception&){rejected=true;}requireIsf(rejected&&tiny.used()==0,"failed allocation leaked reservation");}

 {
 auto s=source(R"(/*{"INPUTS":[{"NAME":"f","TYPE":"float"},{"NAME":"i","TYPE":"long"},{"NAME":"b","TYPE":"bool"},{"NAME":"p","TYPE":"point2D"},{"NAME":"c","TYPE":"color"}]}*/void main(){gl_FragColor=vec4(1);})");
 IsfRuntime r(context,budget,s,32,32);const auto& d=r.defaults();
 requireIsf(std::get<double>(d.at("f"))==.5,"missing float default changed");requireIsf(std::get<int32_t>(d.at("i"))==0&&std::get<bool>(d.at("b")),"scalar default changed");requireIsf(std::get<std::array<double,2>>(d.at("p"))==std::array<double,2>{0,0},"point default changed");requireIsf(std::get<std::array<double,4>>(d.at("c"))==std::array<double,4>{0,0,0,0},"color default changed");
 }
 {IsfBudget small(10000);bool rejected=false;try{auto s=source(R"(/*{"PASSES":[{"TARGET":"history","WIDTH":1024,"HEIGHT":1024,"PERSISTENT":true},{}]}*/void main(){gl_FragColor=vec4(1);})");IsfRuntime r(context,small,s,32,32);IsfParameters p({});p.initializeDefaults(r.defaults());r.draw(p,audio,0,0,0);}catch(const std::exception&){rejected=true;}requireIsf(rejected&&small.used()==0,"internal pass allocation bypassed budget");}

 for(const char* name:{"plasma.fs","spectrum.fs","feedback.fs"}) {
 std::ifstream input(std::string(ISF_ASSET_DIR)+"/examples/"+name);requireIsf(bool(input),"bundled shader missing");std::string text{std::istreambuf_iterator<char>(input),{}};auto s=source(text);IsfRuntime r(context,budget,s,64,64);IsfParameters p(s->document.inputs);p.initializeDefaults(r.defaults());
 auto energy=[&](){r.present(320,180);std::vector<unsigned char> pixels(320*180*4);glReadPixels(0,0,320,180,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());uint64_t sum=0;for(size_t n=0;n<pixels.size();n+=4)sum+=pixels[n]+pixels[n+1]+pixels[n+2];return sum;};
 r.draw(p,audio,0,0,0);auto initial=energy();requireIsf(initial>0,"bundled shader rendered black");
 if(std::string(name)=="spectrum.fs"){IsfAudio sine;std::vector<float> pcm(2048);for(int n=0;n<1024;++n)pcm[n*2]=std::sin(2*3.141592653589793*32*n/1024);sine.append(pcm.data(),1024);r.draw(p,sine,.1,.1,1);requireIsf(energy()>initial,"bundled spectrum did not react to sine");}
 if(std::string(name)=="feedback.fs"){r.draw(p,audio,0,0,1);requireIsf(energy()>initial,"bundled feedback did not retain history");std::string why;p.trigger("reset",why);r.draw(p,audio,0,0,2);requireIsf(energy()==initial,"bundled feedback reset failed");}
 }
 for(const char* name:{"julia.fs","dive.fs","kleinian.fs","phoenix.fs","nova.fs","newton.fs","burning_ship.fs","multibrot.fs","nebula.fs"}) {
 std::ifstream input(std::string(ISF_ASSET_DIR)+"/fractal/"+name);requireIsf(bool(input),"bundled fractal missing");std::string text{std::istreambuf_iterator<char>(input),{}};auto s=source(text);
 IsfAudio loud;std::vector<float> pcm(4096);for(int n=0;n<2048;++n)pcm[n*2]=pcm[n*2+1]=.5f*float(std::sin(2*3.141592653589793*120*n/48000.0));loud.append(pcm.data(),2048);
 // Run 20 frames at the 0.1 s step limit so the integrated animation state visibly advances.
 auto run=[&](const IsfAudio& input,uint64_t frames){IsfRuntime r(context,budget,s,64,64);IsfParameters p(s->document.inputs);p.initializeDefaults(r.defaults());for(uint64_t f=0;f<frames;++f)r.draw(p,input,f*.1,.1,f);r.present(320,180);std::vector<unsigned char> pixels(320*180*4);glReadPixels(0,0,320,180,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());return pixels;};
 auto first=run(audio,2),later=run(audio,20),reacting=run(loud,20);
 uint64_t lit=0;for(size_t n=0;n<later.size();n+=4)lit+=later[n]+later[n+1]+later[n+2];requireIsf(lit>320*180*30,"bundled fractal rendered black");
 requireIsf(first!=later,"bundled fractal did not animate in silence");requireIsf(reacting!=later,"bundled fractal ignored the microphone");
 }
 {
 auto s=source("/*{}*/void main(){gl_FragColor=vec4(1,0,0,1);}");
 IsfRuntime r(context,budget,s,32,32);IsfParameters p({});p.initializeDefaults(r.defaults());
 r.draw(p,audio,0,0,0);
 {fluid_gl::Program departing("void main(){gl_Position=vec4(0);}","out vec4 c;void main(){c=vec4(1);}");departing.use();}
 r.present(320,180);r.draw(p,audio,.1,.1,1);r.present(320,180);requireIsf(pixel(160,90)[0]==255,"departing mode program broke ISF");
 }
 {
 IsfBudget limited(20000);auto s=source("/*{}*/void main(){gl_FragColor=vec4(1,0,0,1);}");IsfRuntime r(context,limited,s,32,32);IsfParameters p({});p.initializeDefaults(r.defaults());r.draw(p,audio,0,0,0);std::string why;
 requireIsf(!r.update(p,48,48,why),"resize admitted without complete frame budget");r.present(320,180);requireIsf(pixel(160,90)[0]==255,"rejected resize lost picture");r.draw(p,audio,.1,.1,1);
 }
 {
 auto s=source(R"(/*{"INPUTS":[{"NAME":"size","TYPE":"float","DEFAULT":2048}],"PASSES":[{"TARGET":"history","WIDTH":"$size","HEIGHT":1024,"FLOAT":true,"PERSISTENT":true},{}]}*/void main(){gl_FragColor=vec4(1,0,0,1);})");IsfRuntime r(context,budget,s,32,32);IsfParameters p(s->document.inputs);p.initializeDefaults(r.defaults());r.draw(p,audio,0,0,0);auto changed=p;std::string why;changed.set({"size",{2500.0f}},why);bool accepted=r.update(changed,32,32,why);r.draw(accepted?changed:p,audio,.1,.1,1);r.present(320,180);requireIsf(pixel(160,90)[0]==255,"rejected growth lost running picture");
 }
 {
 IsfBudget limited(16384);auto s=source(R"(/*{"INPUTS":[{"NAME":"reset","TYPE":"event"}],"PASSES":[{"TARGET":"history","PERSISTENT":true},{}]}*/void main(){if(PASSINDEX==0)gl_FragColor=vec4(reset?0.7:IMG_THIS_PIXEL(history).r+0.1,0,0,1);else gl_FragColor=IMG_THIS_PIXEL(history);})");IsfRuntime r(context,limited,s,32,32);IsfParameters p(s->document.inputs);p.initializeDefaults(r.defaults());r.draw(p,audio,0,0,0);auto reset=p;std::string why;reset.trigger("reset",why);bool rejected=false;try{r.draw(reset,audio,0,0,0);}catch(const IsfFrameRejected&){rejected=true;}requireIsf(rejected,"frame-zero redraw peak was not checked");r.present(320,180);requireIsf(pixel(160,90)[0]>=25&&pixel(160,90)[0]<=27,"rejected redraw changed picture");r.draw(p,audio,.1,.1,1);r.present(320,180);requireIsf(pixel(160,90)[0]>=50&&pixel(160,90)[0]<=53,"rejected redraw lost feedback history");
 }
 {
 IsfBudget limited(30000);auto s=source("/*{}*/void main(){gl_FragColor=vec4(1,0,0,1);}");IsfRuntime r(context,limited,s,32,32);IsfParameters p({});p.initializeDefaults(r.defaults());r.draw(p,audio,0,0,0);std::string why;requireIsf(r.update(p,64,32,why),"valid resize rejected");r.present(320,180);requireIsf(pixel(20,90)[0]==0&&pixel(160,90)[0]==255,"staged resize changed retained presentation");r.draw(p,audio,.1,.1,1);r.present(320,180);requireIsf(pixel(20,90)[0]==255,"successful resize did not commit aspect ratio");
 }
 {
 auto s=source(R"(/*{"PASSES":[{"TARGET":"history","PERSISTENT":true,"FLOAT":true}]}*/void main(){gl_FragColor=vec4(FRAMEINDEX==0?2.0:(IMG_THIS_PIXEL(history).r>1.5?1.0:0.0),0,0,1);})");IsfRuntime r(context,budget,s,32,32);IsfParameters p({});p.initializeDefaults(r.defaults());r.draw(p,audio,0,0,0);r.draw(p,audio,.1,.1,1);r.present(320,180);requireIsf(pixel(160,90)[0]==255,"final named FLOAT history lost precision");
 }
 {
 auto s=source(R"(/*{"PASSES":[{"TARGET":"history","PERSISTENT":true,"FLOAT":true,"WIDTH":16,"HEIGHT":32}]}*/void main(){gl_FragColor=vec4(1,0,0,1);})");IsfRuntime r(context,budget,s,32,32);IsfParameters p({});p.initializeDefaults(r.defaults());r.draw(p,audio,0,0,0);r.present(320,180);requireIsf(pixel(100,90)[0]==0&&pixel(160,90)[0]==255,"final named target dimensions ignored");
 }
 std::cout<<"ISF runtime checks passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
