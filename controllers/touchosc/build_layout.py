#!/usr/bin/env python3
"""Generate Sideshow.tosc with Python's standard library (TouchOSC Mk2 XML)."""
from pathlib import Path
import json
import uuid
import zlib
import xml.etree.ElementTree as ET

HERE = Path(__file__).resolve().parent
BG = (0.035, 0.045, 0.065, 1)
PANEL = (0.075, 0.09, 0.12, 1)
TEAL = (0.12, 0.8, 0.7, 1)
BLUE = (0.3, 0.6, 1, 1)
RED = (0.95, 0.25, 0.3, 1)
WHITE = (0.92, 0.95, 1, 1)
MUTED = (0.52, 0.59, 0.68, 1)


def prop(node, key, value):
    kind = 'b' if isinstance(value, bool) else 'i' if isinstance(value, int) else 'f' if isinstance(value, float) else 's'
    if isinstance(value, tuple): kind = 'r' if key == 'frame' else 'c'
    item = ET.SubElement(node.find('properties'), 'property', type=kind)
    ET.SubElement(item, 'key').text = key
    out = ET.SubElement(item, 'value')
    if kind in ('r', 'c'):
        for k, v in zip(('x','y','w','h') if kind=='r' else ('r','g','b','a'), value):
            ET.SubElement(out, k).text = str(v)
    else: out.text = str(int(value)) if kind == 'b' else str(value)


def value(node, key, default):
    v = ET.SubElement(node.find('values'), 'value')
    for k, text in [('key',key), ('locked','0'), ('lockedDefaultCurrent','1' if key=='text' else '0'),
                    ('default',str(default)), ('defaultPull','0')]:
        ET.SubElement(v,k).text=text


def node(parent, typ, name, frame, color=PANEL, **props):
    n = ET.SubElement(parent, 'node', ID=str(uuid.uuid5(uuid.NAMESPACE_URL, 'sideshow/'+name)), type=typ)
    ET.SubElement(n,'properties'); ET.SubElement(n,'values'); ET.SubElement(n,'messages')
    base = dict(name=name, frame=frame, color=color, visible=True, locked=False,
                interactive=typ in ('BUTTON','FADER'), background=True, outline=False,
                grabFocus=typ != 'GROUP', cornerRadius=5.0, orientation=0)
    base.update(props)
    for k,v in base.items(): prop(n,k,v)
    value(n,'touch','false')
    return n


def label(parent, name, text, x,y,w,h, size=16, color=WHITE, align=1):
    n=node(parent,'LABEL',name,(x,y,w,h),background=False,interactive=False,
           textSize=size,textColor=color,textAlignH=align,textAlignV=2,textClip=True,textLength=0)
    value(n,'text',text)
    return n


def lua(v):
    if isinstance(v,list): return '{'+','.join(lua(x) for x in v)+'}'
    return json.dumps(v,ensure_ascii=True)


def button(parent, name, text, x,y,w,h, action, arg=None, color=BLUE, size=15):
    script = "function onValueChanged(key)\n  if key == 'x' and self.values.x == 1 then\n    root:notify("+lua(action)
    if arg is not None: script+=', '+lua(arg)
    script += ")\n  end\nend"
    n=node(parent,'BUTTON',name,(x,y,w,h),color,buttonType=0,press=True,release=True,
           shape=0,valuePosition=False,outline=True,outlineStyle=0,script=script)
    value(n,'x',0)
    label(parent,name+'_label',text,x,y,w,h,size,WHITE,2)
    return n


def field(parent,name,text,x,y,w,h=35):
    button(parent,'edit_'+name,'',x,y,w,h,'edit',name,color=MUTED)
    label(parent,'field_'+name,text,x+10,y,w-20,h,16)


def build():
    doc=ET.Element('lexml',version='3')
    root=node(doc,'GROUP','sideshow',(0,0,1100,820),BG,script=(HERE/'controller.lua').read_text())
    c=ET.SubElement(root,'children')
    label(c,'title','SIDESHOW',24,18,300,40,30)
    label(c,'subtitle','STAGE CONTROL  /  OSC',25,57,450,24,12,MUTED)
    label(c,'network_hint','Connection 1  ·  UDP 9000  ·  Set the Pi host in Connections',500,25,576,26,14,MUTED,3)
    label(c,'target_title','TARGET',24,95,65,34,12,MUTED)
    field(c,'target','all',95,95,165)
    button(c,'all','All',273,95,65,35,'target','all',TEAL)
    for i in range(1,9): button(c,'panel'+str(i),f'{i:02}',348+(i-1)*63,95,55,35,'target',f'panel-{i:02}',TEAL)
    button(c,'ping','Ping',868,95,90,35,'ping',color=TEAL)
    label(c,'no_sync','No auto-sync',974,95,102,35,12,MUTED)
    # Three columns of performance controls.
    for name,x,w in [('output',24,304),('playback',348,356),('isf',724,352)]:
        node(c,'BOX',name+'_card',(x,153,w,440),PANEL)
    label(c,'output_title','OUTPUT',40,166,264,25,13,TEAL)
    label(c,'master_title','Master',42,206,180,26,18)
    label(c,'master_value','100%',220,206,84,26,20,TEAL,3)
    f=node(c,'FADER','master',(42,246,268,42),TEAL,orientation=1,bar=True,cursor=True,grid=False,
           response=0,outline=True,outlineStyle=0,
           script="function onValueChanged(key)\n if key=='x' then root:notify('master',self.values.x) end\nend")
    value(f,'x',1)
    button(c,'blackout','BLACKOUT',42,307,129,44,'int',['blackout',1],RED)
    button(c,'restore','Restore',181,307,129,44,'int',['blackout',0],TEAL)
    label(c,'lock_title','Preset lock',42,376,130,25,16)
    button(c,'lock','Lock',42,410,129,37,'int',['lock',1])
    button(c,'unlock','Unlock',181,410,129,37,'int',['lock',0])
    label(c,'hardcut_title','Beat hard cuts  /  MilkDrop',42,471,268,25,14,MUTED)
    button(c,'hardcut_on','Enable',42,507,129,37,'int',['hardcut',1])
    button(c,'hardcut_off','Disable',181,507,129,37,'int',['hardcut',0])
    label(c,'output_note','Buttons send explicit on/off values.',42,557,268,22,12,MUTED)
    label(c,'playback_title','PLAYBACK',364,166,324,25,13,TEAL)
    for i,(title,mode) in enumerate([('MilkDrop','milkdrop'),('Fluid','fluid'),('Video','video'),('ISF','isf')]):
        button(c,'mode_'+mode,title,364+i*83,207,75,36,'string',['mode',mode],size=13)
    for i,(title,cmd) in enumerate([('Previous','prev'),('Random','random'),('Next','next')]):
        button(c,cmd,title,364+i*111,257,103,40,'send',cmd)
    label(c,'playlist_title','PLAYLIST / FOLDER',364,312,324,22,11,MUTED)
    field(c,'playlist','default',364,337,240)
    button(c,'load_playlist','Load',614,337,74,35,'field','playlist')
    label(c,'preset_title','PRESET / RELATIVE FILE PATH',364,382,324,22,11,MUTED)
    field(c,'preset','examples/plasma.fs',364,407,240)
    button(c,'load_preset','Load',614,407,74,35,'field','preset')
    label(c,'duration_title','DURATION  1–3600 s',364,452,210,22,11,MUTED)
    field(c,'duration','30',364,477,240)
    button(c,'send_duration','Set',614,477,74,35,'duration')
    label(c,'beat_title','BEAT SENSITIVITY  0–2',364,522,210,22,11,MUTED)
    field(c,'beat','1',364,547,240)
    button(c,'send_beat','Set',614,547,74,35,'beat')
    label(c,'isf_title','ISF PARAMETERS',740,166,320,25,13,TEAL)
    label(c,'input_title','INPUT NAME',740,204,320,20,11,MUTED)
    field(c,'input','speed',740,232,320)
    for i,(title,kind) in enumerate([('Float','float'),('Int','integer'),('Bool','bool'),('XY','point'),('RGBA','color')]):
        button(c,'type_'+kind,title,740+i*65,280,60,32,'kind',kind,size=12)
    label(c,'kind_title','TYPE',740,322,40,24,11,MUTED)
    label(c,'field_kind','float',785,322,270,24,14)
    label(c,'values_title','VALUES  /  COMMA OR SPACE SEPARATED',740,355,320,20,11,MUTED)
    field(c,'values','0.5',740,382,320)
    button(c,'send_isf','Send parameter',740,431,320,38,'isf',color=TEAL)
    label(c,'event_title','EVENT NAME',740,487,320,20,11,MUTED)
    field(c,'event','reset',740,517,218)
    button(c,'send_event','Trigger',970,517,90,35,'event')
    label(c,'isf_note','Select ISF mode and a shader first.',740,562,320,20,12,MUTED)
    # Mode shortcuts.
    label(c,'persistence_title','FLUID PERSISTENCE',790,603,200,20,11,MUTED)
    label(c,'persistence_value','1.0x',1000,603,76,20,13,TEAL,3)
    f=node(c,'FADER','persistence',(790,625,286,20),TEAL,orientation=1,bar=True,cursor=True,
           response=0,outline=True,outlineStyle=0,
           script="function onValueChanged(key)\n if key=='x' then root:notify('persistence',self.values.x) end\nend")
    value(f,'x',0)
    label(c,'video_title','VIDEO',24,608,90,25,13,TEAL)
    for i,(title,act,arg) in enumerate([('Pause','int',['video_pause',1]),('Play','int',['video_pause',0]),('Restart','send','video_restart'),('Loop','string',['video_end','loop']),('Hold','string',['video_end','hold']),('Next at end','string',['video_end','next'])]):
        button(c,'video_'+str(i),title,105+i*110,606,102,32,act,arg,size=13)
    label(c,'fluid_title','FLUID',24,653,78,25,13,TEAL)
    presets=['light-show','lava','aurora','nebula','ember','neon','oil-slick','ink','frost','wild','pompeii','overhead']
    for i,p in enumerate(presets): button(c,'fluid_'+p,p.replace('-',' ').title(),105+i*81,651,75,32,'fluid',p,size=11)
    for i in range(8):
        label(c,'panel_reply_'+str(i+1),'Ping to see panel IDs, presets and FPS.' if i==0 else '',
              24+(i//4)*536,695+(i%4)*18,516,18,12,MUTED)
    label(c,'status','Ready. Configure OSC connection 1, then Ping.',24,779,1052,25,13,TEAL)
    # TouchOSC text controls are display-only; supply a built-in editing keyboard.
    k=node(c,'GROUP','keyboard',(0,0,1100,820),BG,visible=False,interactive=True)
    kc=ET.SubElement(k,'children')
    node(kc,'BOX','keyboard_block',(0,0,1100,820),BG,interactive=True)
    label(kc,'keyboard_title','EDIT',70,145,960,38,20,TEAL)
    label(kc,'keyboard_buffer','',70,200,960,60,23)
    rows=['1234567890-_=+','qwertyuiop[]','asdfghjkl;\'','zxcvbnm,./']
    rows.append('()&!?@#$%*:{}<>')
    rows.append('\"\\|~`^')
    for row,chars in enumerate(rows):
        for i,char in enumerate(chars): button(kc,f'key_{row}_{i}',char,70+i*69,275+row*40,61,34,'key',char,size=20)
    for name,title,x,w in [('shift','Shift',70,140),('back','Backspace',225,190),('clear','Clear',430,140),(' ','Space',585,225)]:
        button(kc,'keyboard_'+name if name!=' ' else 'keyboard_space',title,x,526,w,44,'key',name)
    button(kc,'keyboard_cancel','Cancel',70,605,200,46,'key','cancel',MUTED)
    button(kc,'keyboard_done','Done',830,605,200,46,'key','done',TEAL)
    label(kc,'keyboard_note','Edits stay local until you press Load, Set, Send parameter, or Trigger.',70,675,960,30,15,MUTED)
    ET.indent(doc,space='  ')
    xml=ET.tostring(doc,encoding='utf-8',xml_declaration=True)
    (HERE/'Sideshow.tosc').write_bytes(zlib.compress(xml))
    print(f'Built Sideshow.tosc: {len(list(doc.iter("node")))} controls')
    return doc

if __name__=='__main__': build()
