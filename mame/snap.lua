-- Take an LCD snapshot every N seconds of emulated time, then exit.
-- Used by mame/run_headless.sh.
local interval = tonumber(os.getenv("SNAP_INTERVAL") or "5")
local total = tonumber(os.getenv("SNAP_TOTAL") or "20")
local next_t = interval
emu.register_periodic(function()
  local t = manager.machine.time.seconds
  if t >= next_t then
    manager.machine.video:snapshot()
    next_t = next_t + interval
  end
  if t >= total then manager.machine:exit() end
end)
