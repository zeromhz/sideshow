local controls={}
root={findByName=function(_,name)
 if not controls[name] then controls[name]={values={text=name=='field_target' and 'all' or ''}} end
 return controls[name]
end}
local now=100
function getMillis() return now end
local sent={}
function sendOSC(message) sent[#sent+1]=message end
dofile('controllers/touchosc/controller.lua')
init(); update(); assert(#sent==0)
onReceiveNotify('persistence',1/3); update()
assert(#sent==1,'Persistence should send a packet')
assert(sent[1][1]=='/sideshow/all/fluid_persistence')
assert(sent[1][2][1].tag=='f' and sent[1][2][1].value==4)
onReceiveNotify('persistence',1); onReceiveNotify('target','panel-02'); now=200;update()
assert(#sent==1,'Target change must discard queued slider traffic')
onReceiveNotify('persistence',0);now=250;update()
assert(sent[2][1]=='/sideshow/panel-02/fluid_persistence' and sent[2][2][1].value==1)
onReceiveNotify('persistence',0.5);onReceiveNotify('persistence',1);now=260;update()
assert(#sent==2,'Slider should coalesce within interval')
now=300;update();assert(#sent==3 and sent[3][2][1].value==10)
onReceiveNotify('persistence',0/0);now=400;update();assert(#sent==3)
onReceiveNotify('persistence',1);init();now=500;update();assert(#sent==3)
print('TouchOSC persistence tests passed')
onReceiveNotify('ping')
for i=1,8 do
 onReceiveOSC({'/sideshow/pong',{{tag='s',value='panel-0'..i},{tag='s',value='fluid/lava'},{tag='f',value=60}}},{true})
end
now=1000;update()
for i=1,8 do
 assert(controls['panel_reply_'..i] and controls['panel_reply_'..i].values.text:find('panel%-0'..i),'Each panel needs its own visible row')
end
