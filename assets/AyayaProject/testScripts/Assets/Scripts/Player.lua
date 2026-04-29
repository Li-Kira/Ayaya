-- Player.lua
local jumpCooldown = 0.0

function OnCreate()
    -- 使用我们新绑定的引擎原生日志！
    Log.Info("Player spawned! Press SPACE to jump.")
end

function OnUpdate(ts)
    -- 确保实体身上挂载了刚体，否则会报错
    if not entity:HasRigidbody2D() then
        Log.Warn("Player needs a Rigidbody2D to move!")
        return
    end

    local rb2d = entity:GetRigidbody2D()
    local velocity = rb2d:GetLinearVelocity()
    local speed = 10.0
    
    -- 左右物理移动 (施加持续的力)
    if Input.IsKeyPressed(Key.A) then
        rb2d:ApplyLinearImpulse(vec2.new(-speed * ts, 0.0), true)
    end
    if Input.IsKeyPressed(Key.D) then
        rb2d:ApplyLinearImpulse(vec2.new(speed * ts, 0.0), true)
    end

    -- 冷却计时器
    if jumpCooldown > 0 then
        jumpCooldown = jumpCooldown - ts
    end

    -- 跳跃逻辑 (冲量)
    if Input.IsKeyPressed(Key.Space) and jumpCooldown <= 0.0 then
        -- 向上施加瞬间冲量
        rb2d:ApplyLinearImpulse(vec2.new(0.0, 5.0), true)
        jumpCooldown = 1.0 -- 1秒冷却
        Log.Info("Boing! Jump triggered. Velocity Y: " .. tostring(velocity.y))
    end
    
    -- 速度限制 (防止方块飞出宇宙)
    if velocity.x > 5.0 then
        rb2d:SetLinearVelocity(vec2.new(5.0, velocity.y))
    elseif velocity.x < -5.0 then
        rb2d:SetLinearVelocity(vec2.new(-5.0, velocity.y))
    end
end