local cpu = manager.machine.devices[":maincpu"]
local sp = cpu.spaces["program"]
local n = 0
tap = sp:install_write_tap(0x980000, 0x98007f, "ut", function(offset, data, mask)
  if n < 80 then n = n + 1
    local ch = (offset >> 5) & 3
    local reg = (offset & 0x1f) >> 2
    print(string.format("t=%.3f ch%d reg%d <= %02x  pc=%08x", manager.machine.time:as_double(), ch, reg, data & 0xff, cpu.state["PC"].value))
  end
end)
