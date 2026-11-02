local cpu = manager.machine.devices[":maincpu"]
local sp = cpu.spaces["program"]
local n = 0
tw = sp:install_write_tap(0xa80000, 0xa8007f, "sw", function(offset, data, mask)
  if n < 60 then n = n + 1 print(string.format("t=%.3f W reg%d <= %02x pc=%08x", manager.machine.time:as_double(), (offset & 0x7f) >> 2, data & 0xff, cpu.state["PC"].value)) end
end)
local m = 0
tr = sp:install_read_tap(0xa80000, 0xa8007f, "sr", function(offset, data, mask)
  if m < 30 then m = m + 1 print(string.format("t=%.3f R reg%d -> %02x pc=%08x", manager.machine.time:as_double(), (offset & 0x7f) >> 2, data & 0xff, cpu.state["PC"].value)) end
  return data
end)
