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
    posJitter = {
        value = 0.0,
        type  = "float",
        min   = 0.0,
        max   = 5.0,
        label = "Position Jitter"
    },
    rotJitter = {
        value = 0.0,
        type  = "float",
        min   = 0.0,
        max   = 180.0,
        label = "Rotation Jitter (deg)"
    },
}

-- ============================================================
-- State
-- ============================================================
local spawned = {}      -- array of child entities
local layoutNames = { "Grid", "Line", "Fan", "LayerStack" }

-- Deterministic pseudo-random per entity index (0-based)
-- Uses a simple LCG so jitter is stable across rebuilds.
local function randFor(i, seed)
    local s = (i + 1) * 1103515245 + (seed or 12345)
    s = (s * 1103515245 + 12345) % 2147483648
    return s / 2147483648  -- [0, 1)
end

local function jittered(center, jitter, i, seed)
    if jitter <= 0 then return center end
    return center + (randFor(i, seed) - 0.5) * jitter * 2.0
end

-- ============================================================
-- Compute position for entity index i (1-based)
-- ============================================================
local function calcPosition(i, n, layout, sx, sy, dz, pJit)
    local x, y, z
    if layout == 0 then  -- Grid
        local cols = math.ceil(math.sqrt(n))
        local rows = math.ceil(n / cols)
        local hw = (cols - 1) * sx * 0.5
        local hh = (rows - 1) * sy * 0.5
        local c = (i - 1) % cols
        local r = math.floor((i - 1) / cols)
        x = c * sx - hw
        y = r * sy - hh
        z = -r * dz
    elseif layout == 1 then  -- Line
        local hw = (n - 1) * sx * 0.5
        x = (i - 1) * sx - hw
        y = 0.0
        z = -((i - 1) % 3) * dz
    elseif layout == 2 then  -- Fan
        local radius = sx * 2.0
        local angle = (i - 1) * 6.28318 / n
        x = math.cos(angle) * radius
        y = math.sin(angle) * radius
        z = -i * dz * 0.5
    else  -- LayerStack
        x = 0.0
        y = 0.0
        z = -i * dz * 0.25
    end
    return vec3.new(
        jittered(x, pJit, i, 1),
        jittered(y, pJit, i, 2),
        jittered(z, pJit, i, 3))
end

-- ============================================================
-- Reposition all existing children with optional random jitter
-- ============================================================
local function repositionAll()
    local n = #spawned
    local tflat, rflat = {}, {}
    local pJit = posJitter
    local rJit = rotJitter
    -- Plane.prefab default rotation: 90° around X to stand upright facing camera
    local baseRotX, baseRotY, baseRotZ = 1.570796, 0.0, 0.0

    for i = 1, n do
        if spawned[i] and spawned[i]:IsValid() then
            local pos = calcPosition(i, n, layout, spacingX, spacingY, depth, pJit)
            tflat[#tflat + 1] = pos.x
            tflat[#tflat + 1] = pos.y
            tflat[#tflat + 1] = pos.z

            if rJit > 0 then
                rflat[#rflat + 1] = baseRotX + jittered(0, rJit, i, 10)
                rflat[#rflat + 1] = baseRotY + jittered(0, rJit, i, 11)
                rflat[#rflat + 1] = baseRotZ + jittered(0, rJit, i, 12)
            end
        end
    end
    Ayaya.SetTranslationsBatch(spawned, tflat)
    if rJit > 0 then
        Ayaya.SetRotationsBatch(spawned, rflat)
    end
end

-- ============================================================
-- Incremental rebuild — add/remove children to match target count
-- ============================================================
function Rebuild(spawnerEntity, sceneRef)
    scene = sceneRef
    local current = #spawned
    local target  = count

    -- Remove excess
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
