-- OITBenchmark.lua
-- Attach to an entity to spawn N instances of a Prefab as children.
-- Works in Editor mode — tweak parameters in PropertiesPanel.
--
-- Play-mode keyboard controls:
--   Up/Down ±10   Left/Right ±1   R=reset   1-4=layout

-- ============================================================
-- CONFIG — read by PropertiesPanel to render UI widgets
-- ============================================================
CONFIG = {
    prefab = {
        value = "13906370418440843244",
        type  = "file",
        ext   = ".prefab",
        label = "Prefab"
    },
    count = {
        value = 10,
        type  = "int",
        min   = 0,
        max   = 200,
        label = "Count"
    },
    layout = {
        value   = 0,
        type    = "combo",
        options = { "Grid", "Line", "Fan", "LayerStack" },
        label   = "Layout"
    },
    spacingX = {
        value = 2.0,
        type  = "float",
        min   = 0.1,
        max   = 20.0,
        label = "Spacing X"
    },
    spacingY = {
        value = 2.0,
        type  = "float",
        min   = 0.1,
        max   = 20.0,
        label = "Spacing Y"
    },
    depth = {
        value = 0.3,
        type  = "float",
        min   = 0.01,
        max   = 5.0,
        label = "Depth Offset"
    },
}

-- ============================================================
-- State
-- ============================================================
local spawned = {}      -- array of child entities
local layoutNames = { "Grid", "Line", "Fan", "LayerStack" }

-- ============================================================
-- Compute position for entity index i (1-based)
-- ============================================================
local function calcPosition(i, n, layout, sx, sy, dz)
    if layout == 0 then  -- Grid
        local cols = math.ceil(math.sqrt(n))
        local rows = math.ceil(n / cols)
        local hw = (cols - 1) * sx * 0.5
        local hh = (rows - 1) * sy * 0.5
        local c = (i - 1) % cols
        local r = math.floor((i - 1) / cols)
        return vec3.new(c * sx - hw, r * sy - hh, -r * dz)
    elseif layout == 1 then  -- Line
        local hw = (n - 1) * sx * 0.5
        return vec3.new((i - 1) * sx - hw, 0.0, -((i - 1) % 3) * dz)
    elseif layout == 2 then  -- Fan
        local radius = sx * 2.0
        local angle = (i - 1) * 6.28318 / n
        return vec3.new(math.cos(angle) * radius, math.sin(angle) * radius, -i * dz * 0.5)
    else  -- LayerStack
        return vec3.new(0.0, 0.0, -i * dz * 0.25)
    end
end

-- ============================================================
-- Reposition all existing children (no spawn/despawn, just move)
-- ============================================================
local function repositionAll()
    local n = #spawned
    -- Flat array {x1,y1,z1, x2,y2,z2, ...} — parses 3× faster than table-of-vec3
    local flat = {}
    for i = 1, n do
        if spawned[i] and spawned[i]:IsValid() then
            local pos = calcPosition(i, n, layout, spacingX, spacingY, depth)
            flat[#flat + 1] = pos.x
            flat[#flat + 1] = pos.y
            flat[#flat + 1] = pos.z
        end
    end
    Ayaya.SetTranslationsBatch(spawned, flat)
end

-- ============================================================
-- Incremental rebuild — add/remove children to match target count
-- ============================================================
function Rebuild(spawnerEntity, sceneRef)
    scene = sceneRef
    local current = #spawned
    local target  = count

    -- Remove excess (guard against stale references)
    while #spawned > target do
        local e = spawned[#spawned]
        spawned[#spawned] = nil
        if e and e:IsValid() then e:Destroy() end
    end

    -- Add missing
    while #spawned < target do
        local ok, e = pcall(function() return scene:InstantiatePrefab(prefab) end)
        if not ok or not e or not e:IsValid() then
            Log.Warn("InstantiatePrefab failed at index " .. (#spawned + 1))
            break
        end
        e:SetParent(spawnerEntity)
        table.insert(spawned, e)
    end

    -- Reposition ALL children for the current layout
    repositionAll()

    Log.Info("OIT: " .. #spawned .. " planes, layout=" .. layoutNames[layout + 1])
end

-- ============================================================
-- Play-mode lifecycle
-- ============================================================
function OnCreate()
    Log.Info("=== OIT Benchmark ===")
    Log.Info("  Prefab: " .. tostring(prefab) .. "  Count: " .. count)
    if not scene then
        Log.Error("scene ref is nil")
        return
    end
    Rebuild(entity, scene)
end

local dirtyKeys = {}
function OnUpdate(ts)
    local function pressed(key)
        local down = Input.IsKeyPressed(key)
        local prev = dirtyKeys[key] or false
        dirtyKeys[key] = down
        return down and not prev
    end

    local ch = false
    if pressed(Key.Up)    then count = count + 10; ch = true end
    if pressed(Key.Down)  then count = math.max(0, count - 10); ch = true end
    if pressed(Key.Right) then count = count + 1;  ch = true end
    if pressed(Key.Left)  then count = math.max(0, count - 1); ch = true end
    if pressed(Key.R)     then count = 10; layout = 0; ch = true end
    if pressed(Key._1)    then layout = 0; ch = true end
    if pressed(Key._2)    then layout = 1; ch = true end
    if pressed(Key._3)    then layout = 2; ch = true end
    if pressed(Key._4)    then layout = 3; ch = true end

    if ch then Rebuild(entity, scene) end

    for k, _ in pairs(dirtyKeys) do
        if not Input.IsKeyPressed(k) then dirtyKeys[k] = false end
    end
end

function OnEditorUpdate(ts)
    -- Parameters are driven by PropertiesPanel CONFIG widgets;
    -- the C++ side calls Rebuild() on each change.
end
