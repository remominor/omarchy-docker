-- Container monitor policy. Keep DRM/GBM available for accelerated rendering,
-- but do not let physical connectors claim workspaces in this Moonlight-only
-- session. The named virtual output is configured explicitly below.
local output = os.getenv("OMARCHY_OUTPUT_NAME") or "OMARCHY"
local resolution = os.getenv("OMARCHY_RESOLUTION") or "2560x1440"
local refresh = os.getenv("OMARCHY_REFRESH") or "60"
local scale = tonumber(os.getenv("OMARCHY_SCALE") or "1") or 1

hl.monitor({ output = "", disabled = true })
hl.monitor({
  output = output,
  mode = resolution .. "@" .. refresh,
  position = "0x0",
  scale = scale,
})
