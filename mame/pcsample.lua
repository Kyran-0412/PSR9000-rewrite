local cpu = manager.machine.devices[":maincpu"]
local hist, prs = {}, {}
local n = 0
emu.register_periodic(function()
  local t = manager.machine.time.seconds
  if t > 60 then
    local pc = cpu.state["PC"].value
    local pr = cpu.state["PR"].value
    hist[pc] = (hist[pc] or 0) + 1
    prs[pc] = pr
    n = n + 1
  end
  if t > 75 then
    local arr = {}
    for k, v in pairs(hist) do arr[#arr+1] = {k, v} end
    table.sort(arr, function(a, b) return a[2] > b[2] end)
    print("samples", n)
    for i = 1, math.min(25, #arr) do print(string.format("pc %08x  %5d  pr %08x", arr[i][1], arr[i][2], prs[arr[i][1]])) end
    manager.machine:exit()
  end
end)
