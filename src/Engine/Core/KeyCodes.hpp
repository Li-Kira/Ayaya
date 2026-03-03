#pragma once

namespace Ayaya {
    using KeyCode = int;

    namespace Key {
        enum : KeyCode {
            // 摘选部分常用按键，参考 glfw3.h
            Space         = 32,
            Escape        = 256,
            Enter         = 257,
            Left          = 263,
            Right         = 262,
            Up            = 265,
            Down          = 264,
            A             = 65,
            D             = 68,
            S             = 83,
            W             = 87
        };
    }

    namespace Mouse {
        enum : KeyCode {
            Button0       = 0,
            Button1       = 1,
            ButtonLeft    = Button0,
            ButtonRight   = Button1
        };
    }
}