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
print('TouchOSC envelope, correlation, timeout, broadcast, shortcut and coalescing tests passed')
