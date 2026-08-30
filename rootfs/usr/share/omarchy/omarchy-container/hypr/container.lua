-- Container monitor policy. Keep physical connectors disabled before they can
-- claim workspaces during an Omarchy display-settings reload. Read the scale
-- from the user's monitors.lua so the display utility remains authoritative.
local output = os.getenv("OMARCHY_OUTPUT_NAME") or "OMARCHY"
local resolution = os.getenv("OMARCHY_RESOLUTION") or "2560x1440"
local refresh = os.getenv("OMARCHY_REFRESH") or "60"
local scale = tonumber(os.getenv("OMARCHY_SCALE") or "1") or 1

local monitors = io.open((os.getenv("HOME") or "/home/omarchy") .. "/.config/hypr/monitors.lua", "r")
if monitors then
  local config = monitors:read("*a")
  monitors:close()
  local configured_scale = config:match("omarchy_monitor_scale%s*=%s*([%d%.]+)")
  if configured_scale then
    scale = tonumber(configured_scale) or scale
  end
end

hl.monitor({ output = "", disabled = true })
hl.monitor({
  output = output,
  mode = resolution .. "@" .. refresh,
  position = "0x0",
  scale = scale,
})
