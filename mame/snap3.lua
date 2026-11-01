local shots = {}
for s in string.gmatch(os.getenv("SHOTS") or "5 10 20 40", "%S+") do shots[#shots+1] = tonumber(s) end
local i = 1
emu.register_frame_done(function()
  local t = manager.machine.time.seconds
  if i <= #shots and t >= shots[i] then
    manager.machine.video:snapshot(); i = i + 1
  end
end)
