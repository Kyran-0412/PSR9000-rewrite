local done = 0
local f = io.open("/tmp/emu/dial.log", "w")
emu.register_periodic(function()
  local now = manager.machine.time:as_double()
  local fld = manager.machine.ioport.ports[":DIAL"].fields["Data Dial"]
  if done == 0 and now >= 5 then fld:set_value(tonumber(os.getenv("D1") or "250")); done = 1; f:write("set\n")
  elseif done == 1 and now >= 6 then manager.machine.video:snapshot(); done = 2
  elseif done == 2 and now >= 6.5 then fld:set_value(tonumber(os.getenv("D2") or "10")); done = 3
  elseif done == 3 and now >= 7.5 then manager.machine.video:snapshot(); done = 4 end
end)
