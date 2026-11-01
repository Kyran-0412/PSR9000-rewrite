local cpu = manager.machine.devices[":maincpu"]
local sp = cpu.spaces["program"]
local n = 0
tap = sp:install_read_tap(0xc00000, 0xc00003, "c0tap", function(offset, data, mask)
  if n < 12 then
    n = n + 1
    local s = cpu.state
    print(string.format("read %08x mask %08x pc=%08x pr=%08x r15=%08x r0=%08x gbr=%08x vbr=%08x sr=%08x t=%f",
      offset, mask, s["PC"].value, s["PR"].value, s["R15"].value, s["R0"].value, s["GBR"].value, s["VBR"].value, s["SR"].value, manager.machine.time.seconds))
  end
  return data
end)
