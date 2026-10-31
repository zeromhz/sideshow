local controls={}
root={findByName=function(_,name)
 if not controls[name] then controls[name]={values={text=name=='field_target' and 'all' or name=='field_reply_port' and '9001' or ''}} end
 return controls[name]
end}
local now=100
function getMillis() return now end
local sent={}
function sendOSC(message,connections) assert(connections[1]);sent[#sent+1]=message end
dofile('controllers/touchosc/controller.lua')
local function text(name) return controls[name].values.text end
local function ack(packet,panel,state,detail,connection)
 onReceiveOSC({'/sideshow/ack',{{tag='s',value=packet[2][1].value},{tag='s',value=panel},{tag='s',value=state},{tag='s',value=detail}}},connection or {true})
end
local function envelope(packet,address,value)
 assert(packet[1]=='/sideshow/request')
 assert(packet[2][1].tag=='s' and #packet[2][1].value<=64)
 assert(packet[2][2].tag=='i' and packet[2][2].value==9001)
 assert(packet[2][3].tag=='s' and packet[2][3].value==address)
 assert(packet[2][4].tag=='f' and packet[2][4].value==value)
end
init();update();assert(#sent==0)
onReceiveNotify('persistence',1/3);update();envelope(sent[1],'/sideshow/all/fluid_persistence',4)
ack(sent[1],'panel-01','applied','4',{false,true});assert(text('ack_1_1')=='')
ack(sent[1],'panel-01','applied','4');ack(sent[1],'panel-02','rejected','unsupported')
assert(text('ack_1_1'):find('panel%-01') and text('ack_1_2'):find('rejected'))
ack(sent[1],'panel-01','rejected','duplicate');assert(text('ack_1_1'):find('applied'))
now=2200;update();assert(text('ack_title_1'):find('partial'))
ack(sent[1],'panel-03','applied','late');assert(text('ack_1_3')=='')
onReceiveNotify('persistence',1);onReceiveNotify('target','panel-02');now=2300;update();assert(#sent==1)
onReceiveNotify('persistence',0);now=2350;update();envelope(sent[2],'/sideshow/panel-02/fluid_persistence',1)
ack(sent[2],'panel-01','applied','wrong target');assert(text('ack_1_1')=='')
onReceiveNotify('persistence',.5);onReceiveNotify('persistence',1);now=2360;update();assert(#sent==2)
now=2400;update();envelope(sent[3],'/sideshow/panel-02/fluid_persistence',10)
assert(sent[2][2][1].value~=sent[3][2][1].value)
ack(sent[2],'panel-02','rejected','old request');assert(text('ack_1_1')=='')
now=4500;update();assert(text('ack_title_1'):find('No reply'))
onReceiveNotify('persistence',0/0);update();assert(#sent==3)
onReceiveNotify('persistence',1);init();now=4600;update();assert(#sent==3)
onReceiveNotify('fluid','lava');assert(#sent==5)
ack(sent[5],'panel-02','accepted','queued');ack(sent[4],'panel-02','rejected','mode unavailable')
assert(text('ack_1_1'):find('rejected') and text('ack_2_1'):find('accepted'))
assert(text('ack_2_1'):find('not complete'))
onReceiveNotify('edit','reply_port');onReceiveNotify('key','clear');onReceiveNotify('key','9');onReceiveNotify('key','2');onReceiveNotify('key','0');onReceiveNotify('key','0');onReceiveNotify('key','done')
onReceiveNotify('send','next');assert(sent[6][2][2].value==9200)
ack(sent[6],'panel-02','superseded','newer request');assert(text('ack_1_1'):find('superseded'))
onReceiveNotify('edit','reply_port');onReceiveNotify('key','clear');onReceiveNotify('key','0');onReceiveNotify('key','done');onReceiveNotify('send','next');assert(#sent==6)
onReceiveNotify('ping');assert(sent[7][1]=='/sideshow/ping' and #sent[7][2]==0)
for i=1,8 do
 onReceiveOSC({'/sideshow/pong',{{tag='s',value='panel-0'..i},{tag='s',value='fluid/lava'},{tag='f',value=60}}},{true})
end
now=5000;update()
for i=1,8 do assert(text('panel_reply_'..i):find('panel%-0'..i)) end
-- Malformed/unknown responses do not impersonate the current request.
controls.field_reply_port.values.text='9001';init();onReceiveNotify('target','all');onReceiveNotify('send','next')
local current=sent[#sent]
ack({[2]={{value='unknown'}}},'panel-01','applied','unknown')
onReceiveOSC({'/sideshow/ack',{{tag='s',value=current[2][1].value}}},{true})
ack(current,'panel-01','bogus','invalid status')
assert(text('ack_1_1')=='')
for i=1,9 do ack(current,'panel-'..i,'applied','next') end
assert(text('ack_1_8'):find('panel%-8'))
assert(not text('ack_title_1'):find('9 replies'))
-- More outstanding traffic than the cap remains bounded and old replies stay irrelevant.
for i=1,140 do onReceiveNotify('send','next') end
ack(current,'panel-0','rejected','evicted');assert(text('ack_1_1')=='')
local latest=sent[#sent];now=now+2000;update()
ack(latest,'panel-01','applied','exact deadline');assert(text('ack_1_1')=='')
assert(text('ack_title_1'):find('No reply'))
-- Fractals panel: open/close, shader shortcuts, rotate, and coalesced per-input sliders.
controls.field_reply_port.values.text='9001';init();onReceiveNotify('target','all')
assert(controls.fractal_panel.visible==false)
onReceiveNotify('fractal_panel','open');assert(controls.fractal_panel.visible==true)
local base=#sent
onReceiveNotify('fractal','nova')
assert(sent[base+1][2][3].value=='/sideshow/all/mode' and sent[base+1][2][4].value=='isf')
assert(sent[base+2][2][3].value=='/sideshow/all/preset' and sent[base+2][2][4].value=='fractal/nova.fs')
onReceiveNotify('fractal','rotate')
assert(sent[base+4][2][3].value=='/sideshow/all/playlist' and sent[base+4][2][4].value=='fractal')
onReceiveNotify('fractal','../etc');onReceiveNotify('fractal',{'x'});assert(#sent==base+4)
base=#sent;now=now+100
onReceiveNotify('fractal_param',{'speed',.1});onReceiveNotify('fractal_param',{'speed',.5})
onReceiveNotify('fractal_param',{'zoom',1});onReceiveNotify('fractal_param',{'bogus',.5})
onReceiveNotify('fractal_param',{'detail',0/0});onReceiveNotify('fractal_param',.5)
assert(text('fractal_speed_value')=='2.00' and text('fractal_zoom_value')=='4.00')
update();assert(#sent==base+2)
assert(sent[base+1][2][3].value=='/sideshow/all/isf_param' and sent[base+1][2][4].value=='speed' and sent[base+1][2][5].value==2)
assert(sent[base+2][2][4].value=='zoom' and sent[base+2][2][5].value==4)
onReceiveNotify('fractal_param',{'speed',1});now=now+10;update();assert(#sent==base+2)
now=now+30;update();assert(#sent==base+3 and sent[base+3][2][5].value==4)
onReceiveNotify('fractal_param',{'color',1});onReceiveNotify('target','panel-01');now=now+100;update();assert(#sent==base+3)
onReceiveNotify('fractal_panel','close');assert(controls.fractal_panel.visible==false)
print('TouchOSC envelope, correlation, timeout, broadcast, shortcut, coalescing and fractal panel tests passed')
