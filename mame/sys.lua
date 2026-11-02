local cpu = manager.machine.devices[":maincpu"]
local dbg = manager.machine.debugger
local f = io.open("/tmp/emu/sys.log", "w")
local started = false
local n = 0
emu.register_periodic(function()
  if not started then
    started = true
    cpu.debug:bpset(0x2105c, "", "")
    dbg.execution_state = "run"
    return
  end
end)
emu.register_frame_done(function() end)
emu.register_periodic(function() end)
-- poll stopped state frequently via a timer-like loop
emu.add_machine_pause_notifier(function() end)
local function check()
  while dbg.execution_state == "stop" do
    local s = cpu.state
    n = n + 1
    f:write(string.format("%.4f r0=%08x r4=%08x r5=%08x pr=%08x sp=%08x\n", manager.machine.time:as_double(), s["R0"].value, s["R4"].value, s["R5"].value, s["PR"].value, s["R15"].value))
    dbg.execution_state = "run"
    return
  end
end
emu.register_periodic(check)
