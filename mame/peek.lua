local cpu = manager.machine.devices[":maincpu"]
local sp = cpu.spaces["program"]
local done = false
emu.register_periodic(function()
  if not done and manager.machine.time.seconds > 20 then
    done = true
    local b = sp:read_u32(0x10f3150); local s = sp:read_u32(0x10f3154)
    print(string.format("base(10f3150)=%08x  struct(10f3154)=%08x", b, s))
    for i = 0, 15 do io.write(string.format("%08x ", sp:read_u32(s + i*4))) end print()
    print(string.format("struct+0xC0.. : %08x %08x %08x %08x", sp:read_u32(s+0xc0), sp:read_u32(s+0xc4), sp:read_u32(s+0xc8), sp:read_u32(s+0xcc)))
    manager.machine:exit()
  end
end)
