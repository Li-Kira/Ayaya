-- GDRBenchmark.lua
-- GPU-Driven Rendering stress test — spawns N prefab instances to benchmark GDR.
-- Attach to an empty entity; tweak parameters in PropertiesPanel.
--
-- Play-mode keyboard controls:
--   Up/Down ±100    Left/Right ±10    W/S ±1    R=reset
--   G=Grid  L=Line  C=Circle  X=Sphere  F=toggle FPS

CONFIG = {
    prefab = {
        value = "17729433601073590700", type = "file", ext = ".prefab", label = "Prefab"
    },
    count = {
        value = 100, type = "int", min = 0, max = 10000, label = "Count"
    },
    layout = {
        value = 0, type = "combo",
        options = { "Grid", "Line", "Circle", "RandomSphere" }, label = "Layout"
    },
    spacing =  { value = 3.0, type = "float", min = 0.5, max = 50.0,  label = "Spacing" },
    radius =   { value = 10.0, type = "float", min = 1.0, max = 200.0, label = "Radius" },
    posJitter = { value = 0.0, type = "float", min = 0.0, max = 10.0,  label = "Position Jitter" },
    rotJitter = { value = 0.0, type = "float", min = 0.0, max = 360.0, label = "Rotation Jitter (deg)" },
}

local spawned = {}
local DEG2RAD = 0.01745329252

-- Deterministic LCG per index
local function randFor(i, seed)
    local s = (i + 1) * 1103515245 + (seed or 12345)
    s = (s * 1103515245 + 12345) % 2147483648
    return s / 2147483648
end

local function jittered(v, jit, i, seed)
    if jit <= 0 then return v end
    return v + (randFor(i, seed) - 0.5) * jit * 2.0
end

local function calcPosition(i, n, lay, sp, rad, pJit)
    local x, y, z
    if lay == 0 then
        local cols = math.ceil(math.sqrt(n))
        local hw = (cols - 1) * sp * 0.5
        local c = (i - 1) % cols; local r = math.floor((i - 1) / cols)
        x = c * sp - hw; y = 0.0; z = -r * sp
    elseif lay == 1 then
        local hw = (n - 1) * sp * 0.5
        x = (i - 1) * sp - hw; y = 0.0; z = 0.0
    elseif lay == 2 then
        local a = (i - 1) * 6.283185307 / n
        x = math.cos(a) * rad; y = 0.0; z = math.sin(a) * rad
    else
        local phi = math.acos(1.0 - 2.0 * randFor(i, 100))
        local theta = 6.283185307 * randFor(i, 200)
        x = math.sin(phi) * math.cos(theta) * rad
        y = math.sin(phi) * math.sin(theta) * rad
        z = math.cos(phi) * rad
    end
    return vec3.new(
        jittered(x, pJit, i, 1), jittered(y, pJit, i, 2), jittered(z, pJit, i, 3))
end

-- Batch reposition all
local function repositionAll()
    local n = #spawned
    if n == 0 then return end
    local sp, rad, lay, pJit, rJit = spacing, radius, layout, posJitter, rotJitter
    local tflat, rflat = {}, {}
    local ri = 0

    for i = 1, n do
        if spawned[i] and spawned[i]:IsValid() then
            local pos = calcPosition(i, n, lay, sp, rad, pJit)
            local base = (i - 1) * 3
            tflat[base + 1] = pos.x; tflat[base + 2] = pos.y; tflat[base + 3] = pos.z
            if rJit > 0.01 then
                rflat[ri + 1] = jittered(0, rJit, i, 10) * DEG2RAD
                rflat[ri + 2] = jittered(0, rJit, i, 11) * DEG2RAD
                rflat[ri + 3] = jittered(0, rJit, i, 12) * DEG2RAD
                ri = ri + 3
            end
        end
    end
    if #tflat > 0 then Ayaya.SetTranslationsBatch(spawned, tflat) end
    if #rflat > 0 then Ayaya.SetRotationsBatch(spawned, rflat) end
end

-- Rebuild — destroy all + create all (blocking, but correct)
function Rebuild(spawnerEntity, sceneRef)
    scene = sceneRef
    local current = #spawned
    local target  = count
    if target < 0 then target = 0 end

    -- Destroy all existing
    for i = #spawned, 1, -1 do
        local e = spawned[i]
        spawned[i] = nil
        if e and e:IsValid() then e:Destroy() end
    end

    -- Create all
    for i = 1, target do
        local e = scene:InstantiatePrefab(prefab)
        if not e or not e:IsValid() then
            Log.Warn("GDRBench: spawn failed at " .. i)
            break
        end
        e:SetParent(spawnerEntity)
        spawned[#spawned + 1] = e
    end

    repositionAll()
    Log.Info("GDRBench: " .. #spawned .. " instances")
end

local dirtyKeys = {}
local frameCounter = 0
local printFPS = false

function OnCreate()
    Log.Info("=== GDR Benchmark ===")
    if not scene then Log.Error("scene is nil"); return end
    Rebuild(entity, scene)
end

function OnUpdate(ts)
    local function pressed(key)
        local down = Input.IsKeyPressed(key)
        local prev = dirtyKeys[key] or false
        dirtyKeys[key] = down
        return down and not prev
    end

    local ch = false
    if pressed(Key.Up)    then count = count + 100; ch = true end
    if pressed(Key.Down)  then count = math.max(0, count - 100); ch = true end
    if pressed(Key.Right) then count = count + 10;  ch = true end
    if pressed(Key.Left)  then count = math.max(0, count - 10); ch = true end
    if pressed(Key.W)     then count = count + 1;   ch = true end
    if pressed(Key.S)     then count = math.max(0, count - 1); ch = true end
    if pressed(Key.R)     then count = 100; layout = 0; ch = true end
    if pressed(Key.G)     then layout = 0; ch = true end
    if pressed(Key.L)     then layout = 1; ch = true end
    if pressed(Key.C)     then layout = 2; ch = true end
    if pressed(Key.X)     then layout = 3; ch = true end
    if pressed(Key.F)     then printFPS = not printFPS end

    if ch then Rebuild(entity, scene) end

    if printFPS then
        frameCounter = frameCounter + 1
        if frameCounter % 120 == 1 then
            local fps = 1.0 / math.max(ts, 0.0001)
            Log.Info(string.format("GDRBench: %d instances | %.1f FPS (%.2f ms)",
                #spawned, fps, ts * 1000.0))
        end
    end

    for k, _ in pairs(dirtyKeys) do
        if not Input.IsKeyPressed(k) then dirtyKeys[k] = false end
    end
end

function OnEditorUpdate(ts) end
