-- BPS="addr,addr" : log r4-r7/pr at each hit; also does PRESS events like press.lua
local cpu = manager.machine.devices[":maincpu"]
local dbg = manager.machine.debugger
local f = io.open("/tmp/emu/bp.log", "w")
local started = false
local ev = {}
for t, row, name in string.gmatch(os.getenv("PRESS") or "", "([%d.]+):(%w+):([^;]+)") do
  ev[#ev+1] = {t=tonumber(t), row=row, name=name, st=0}
end
emu.register_periodic(function()
  if not started then
    started = true
    for a in string.gmatch(os.getenv("BPS") or "", "%x+") do cpu.debug:bpset(tonumber(a, 16), "", "") end
    dbg.execution_state = "run"
    return
  end
  while dbg.execution_state == "stop" do
    local s = cpu.state
    f:write(string.format("%.4f pc=%08x r4=%08x r5=%08x r6=%08x r7=%08x pr=%08x\n", manager.machine.time:as_double(), s["PC"].value, s["R4"].value, s["R5"].value, s["R6"].value, s["R7"].value, s["PR"].value))
    f:flush()
    dbg.execution_state = "run"
  end
end)
emu.register_periodic(function()
  local now = manager.machine.time:as_double()
  for _, e in ipairs(ev) do
    local fl = manager.machine.ioport.ports[":" .. e.row].fields[e.name]
    if e.st == 0 and now >= e.t then fl:set_value(1); e.st = 1; f:write(string.format("%.4f press %s\n", now, e.name))
    elseif e.st == 1 and now >= e.t + 0.15 then fl:set_value(0); e.st = 2; f:write(string.format("%.4f release\n", now))
    elseif e.st == 2 and now >= e.t + 0.8 then manager.machine.video:snapshot(); e.st = 3 end
  end
end)
