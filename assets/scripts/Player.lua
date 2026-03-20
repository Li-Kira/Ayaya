-- Player.lua

function OnCreate()
    print("Player Entity Created from Lua!")
end

function OnUpdate(ts)
    -- 从 C++ 端获取 Transform 组件引用
    local transform = entity:GetTransform()
    local speed = 5.0 * ts

    -- 控制移动 (这里的 Input.IsKeyPressed 是直接呼叫的 C++ 底层 GLFW)
    if Input.IsKeyPressed(Key.W) then
        transform.Translation.y = transform.Translation.y + speed
    end
    if Input.IsKeyPressed(Key.S) then
        transform.Translation.y = transform.Translation.y - speed
    end
    if Input.IsKeyPressed(Key.A) then
        transform.Translation.x = transform.Translation.x - speed
    end
    if Input.IsKeyPressed(Key.D) then
        transform.Translation.x = transform.Translation.x + speed
    end
end