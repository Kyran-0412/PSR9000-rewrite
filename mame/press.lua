-- PRESS="t:ROW:NAME;..." presses (0.15 s) a panel button at time t; snapshots 0.8 s after each
local ev = {}
for t, row, name in string.gmatch(os.getenv("PRESS") or "", "([%d.]+):(%w+):([^;]+)") do
  ev[#ev+1] = {t=tonumber(t), row=row, name=name, st=0}
end
emu.register_periodic(function()
  local now = manager.machine.time:as_double()
  for _, e in ipairs(ev) do
    local f = manager.machine.ioport.ports[":" .. e.row].fields[e.name]
    if e.st == 0 and now >= e.t then f:set_value(1); e.st = 1
    elseif e.st == 1 and now >= e.t + 0.15 then f:set_value(0); e.st = 2
    elseif e.st == 2 and now >= e.t + 0.8 then manager.machine.video:snapshot(); e.st = 3 end
  end
end)
