-- Scripted panel session for MAME (psr9000).
-- STEPS=file with one command per line:
--   press NAME [| hold] press a panel button by its port name (hold seconds, default 0.15)
--   dial N              turn the data dial N steps (negative = down)
--   wait S              wait S emulated seconds
--   snap LABEL          take a snapshot; LABEL goes to crawl.log with the snapshot number
--   # comment
-- The script starts after START seconds (default 5). Each command after a press/dial
-- waits 0.6 s for the screen to settle.
local path = os.getenv("STEPS")
local steps = {}
for line in io.lines(path) do
  line = line:gsub("^%s+", ""):gsub("%s+$", "")
  if line ~= "" and line:sub(1, 1) ~= "#" then steps[#steps + 1] = line end
end
local log = io.open(os.getenv("CRAWLLOG") or "/tmp/emu/crawl.log", "w")
local fields = {}
for tag, port in pairs(manager.machine.ioport.ports) do
  for name, f in pairs(port.fields) do fields[name] = f end
end
local dialbtn = manager.machine.ioport.ports[":DIALBTN"]
local i, t_next, held, snapn = 1, tonumber(os.getenv("START") or "5"), nil, 0
local dial_left = 0
emu.register_periodic(function()
  local now = manager.machine.time:as_double()
  if held and now >= held.until_t then
    held.f:set_value(0); held = nil
    t_next = now + 0.6
  end
  if dial_left ~= 0 and now >= t_next then
    local f = fields[dial_left > 0 and "Data Dial +" or "Data Dial -"]
    if not held then
      f:set_value(1); held = {f = f, until_t = now + 0.02}
      dial_left = dial_left > 0 and dial_left - 1 or dial_left + 1
      t_next = now + 0.06
    end
    return
  end
  if held or now < t_next or i > #steps then return end
  local cmd, arg, arg2 = steps[i]:match("^(%S+)%s*(.-)%s*([%d%.]*)$")
  local line = steps[i]
  i = i + 1
  if cmd == "press" then
    local name, hold = line:match("^press%s+(.-)%s*|%s*([%d%.]+)$")
    if not name then name = line:match("^press%s+(.+)$"); hold = 0.15 end
    local f = fields[name]
    if not f then log:write("MISSING " .. name .. "\n"); log:flush(); return end
    f:set_value(1); held = {f = f, until_t = now + tonumber(hold)}
  elseif cmd == "dial" then
    dial_left = tonumber(line:match("^dial%s+(%-?%d+)"))
  elseif cmd == "wait" then
    t_next = now + tonumber(line:match("^wait%s+([%d%.]+)"))
  elseif cmd == "snap" then
    manager.machine.video:snapshot()
    log:write(string.format("%04d %.2f %s\n", snapn, now, line:sub(6))); log:flush()
    snapn = snapn + 1
    t_next = now + 0.05
  elseif cmd == "quit" then
    manager.machine:exit()
  end
end)
