-- Sideshow control surface. Connection 1 is the only OSC output.
local fields = {}
local ready = false
local editing, buffer, shift = nil, '', false
local pendingMaster, lastMaster = nil, 0
local pendingPersistence, lastPersistence = nil, 0
local replies, pingDeadline, lastStatus = {}, nil, 0

local function control(name) return root:findByName(name, true) end
local function label(name, text) control(name).values.text = text end
local function trim(s) return (s:gsub('^%s+', ''):gsub('%s+$', '')) end
local function status(text) label('status', text) end
local function finite(n) return n and n == n and n ~= math.huge and n ~= -math.huge end
local function argument(tag, value) return {tag=tag, value=value} end
local function target()
  local id = trim(fields.target or '')
  if #id < 1 or #id > 32 or not id:match('^[a-z0-9][a-z0-9%-]*$') then
    status('Target must be all or a valid panel ID.'); return nil
  end
  return id
end
local function send(command, args)
  local id = target()
  if not id then return end
  sendOSC({'/sideshow/' .. id .. '/' .. command, args or {}}, {true})
  status('Sent to ' .. id .. ': ' .. command .. '  |  not acknowledged')
end
local function setField(name, value)
  fields[name] = value
  label('field_' .. name, value == '' and 'Tap to enter...' or value)
end
local function number(text, kind)
  local n = tonumber(trim(text))
  if not finite(n) then return nil end
  if kind == 'i' then
    if n % 1 ~= 0 or n < -2147483648 or n > 2147483647 then return nil end
  elseif math.abs(n) > 3.4028234663852886e38 then return nil end
  return n
end
local function sendNumber(command, field, low, high)
  local n = number(fields[field], 'f')
  if not n or n < low or n > high then
    status('Enter a number from ' .. low .. ' to ' .. high .. '.'); return
  end
  send(command, {argument('f', n)})
end
local function sendISF()
  local name, kind = trim(fields.input), fields.kind
  if name == '' then status('Enter an ISF input name.'); return end
  local count = ({float=1, integer=1, bool=1, point=2, color=4})[kind]
  local tag = (kind == 'integer' or kind == 'bool') and 'i' or 'f'
  local args = {argument('s', name)}
  local tokens = {}
  for token in fields.values:gmatch('[^,%s]+') do tokens[#tokens+1] = token end
  if #tokens ~= count then status('This type needs ' .. count .. ' value(s).'); return end
  for _, token in ipairs(tokens) do
    local n = number(token, tag)
    if not n or (kind == 'bool' and n ~= 0 and n ~= 1) then
      status('Invalid ' .. kind .. ' value: ' .. token); return
    end
    args[#args+1] = argument(tag, n)
  end
  send('isf_param', args)
end

function init()
  ready = false
  pendingPersistence = nil
  pendingMaster, pingDeadline, replies = nil, nil, {}
  editing, buffer, shift = nil, '', false
  for _, name in ipairs({'target','playlist','preset','duration','beat','input','values','event','kind'}) do
    fields[name] = control('field_' .. name).values.text
  end
  control('keyboard').visible = false
  status('Ready. Configure OSC connection 1, then Ping. Nothing sent on open.')
  ready = true
end

function onReceiveNotify(action, value)
  if not ready then return end
  if action == 'edit' then
    pendingMaster = nil
    pendingPersistence = nil
    editing, buffer, shift = value, fields[value], false
    label('keyboard_title', 'EDIT ' .. string.upper(value) .. '  /  APPLY CHANGES WITH DONE')
    label('keyboard_buffer', buffer .. '|')
    control('keyboard').visible = true
    return
  end
  if action == 'key' then
    if not editing then return end
    if value == 'cancel' then editing=nil; control('keyboard').visible=false; return end
    if value == 'done' then
      setField(editing, buffer); editing=nil; control('keyboard').visible=false; return
    elseif value == 'clear' then buffer=''
    elseif value == 'back' then buffer=buffer:sub(1, -2)
    elseif value == 'shift' then shift=not shift
    elseif #buffer < 512 then buffer=buffer .. (shift and value:upper() or value) end
    label('keyboard_buffer', buffer .. '|')
    label('keyboard_shift_label', shift and 'SHIFT ON' or 'Shift')
    return
  end
  if editing then return end
  if action == 'target' then
    pendingMaster=nil; pendingPersistence=nil; setField('target', value)
    status('Target: ' .. value .. '. Controls show local values, not panel state.')
  elseif action == 'persistence' then
    if not finite(value) then return end
    pendingPersistence = 1 + 9 * math.max(0, math.min(1, value))
    label('persistence_value', string.format('%.1fx', pendingPersistence))
  elseif action == 'master' then
    local n = math.max(0, math.min(1, value))
    pendingMaster = n
    label('master_value', string.format('%d%%', math.floor(n*100+.5)))
  elseif action == 'send' then send(value)
  elseif action == 'int' then send(value[1], {argument('i', value[2])})
  elseif action == 'string' then send(value[1], {argument('s', value[2])})
  elseif action == 'field' then
    local text=trim(fields[value])
    if text == '' then status('Enter a ' .. value .. ' first.'); return end
    send(value, {argument('s', text)})
  elseif action == 'duration' then sendNumber('duration', 'duration', 1, 3600)
  elseif action == 'beat' then sendNumber('beat_sensitivity', 'beat', 0, 2)
  elseif action == 'kind' then
    setField('kind', value)
    setField('values', ({float='0.5', integer='0', bool='1', point='0.5, 0.5', color='1, 1, 1, 1'})[value])
  elseif action == 'isf' then sendISF()
  elseif action == 'event' then
    local name=trim(fields.event)
    if name=='' then status('Enter an event name first.'); return end
    send('isf_event', {argument('s', name)})
  elseif action == 'fluid' then
    send('mode', {argument('s', 'fluid')})
    send('preset', {argument('s', value)})
  elseif action == 'ping' then
    replies={}; pingDeadline=getMillis()+2000
    for i=1,8 do label('panel_reply_' .. i, '') end
    label('panel_reply_1', 'Waiting for panel replies...')
    sendOSC({'/sideshow/ping', {}}, {true})
    status('Ping sent on connection 1.')
  end
end

function onReceiveOSC(message, connections)
  if not ready or not connections[1] or message[1] ~= '/sideshow/pong' then return end
  local a=message[2]
  if #a ~= 3 or a[1].tag ~= 's' or a[2].tag ~= 's' or a[3].tag ~= 'f' then return end
  if not finite(a[3].value) then return end
  -- Bound unsolicited replies. The stage is expected to have 6–8 panels.
  local count=0; for _ in pairs(replies) do count=count+1 end
  if count>=8 and not replies[a[1].value] then return end
  replies[a[1].value]={preset=a[2].value, fps=a[3].value, seen=getMillis()}
end

function update()
  if not ready then return end
  local now=getMillis()
  if pendingPersistence and now-lastPersistence>=34 then
    send('fluid_persistence', {argument('f', pendingPersistence)})
    pendingPersistence=nil; lastPersistence=now
  end
  if pendingMaster and now-lastMaster>=34 then
    send('master', {argument('f', pendingMaster)})
    pendingMaster=nil; lastMaster=now
  end
  if now-lastStatus < 250 then return end
  lastStatus=now
  local ids={}; for id in pairs(replies) do ids[#ids+1]=id end; table.sort(ids)
  if #ids>0 then
    for i,id in ipairs(ids) do
      local r=replies[id]; local age=math.floor((now-r.seen)/1000)
      local preset=r.preset:gsub('[%c]', ' ')
      if #preset>32 then preset=preset:sub(1,29) .. '...' end
      label('panel_reply_' .. i, string.format('%s  %.1f fps  %s  [%ds]', id, r.fps, preset, age))
    end

  elseif pingDeadline and now>=pingDeadline then
    label('panel_reply_1', 'No replies. Check host, port and Pi service.')
    pingDeadline=nil
  end
end
