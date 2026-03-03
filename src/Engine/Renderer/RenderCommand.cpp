#include "RenderCommand.hpp"
#include "Platform/OpenGL/OpenGLRendererAPI.hpp"

namespace Ayaya {

    // 静态变量初始化
    // 以后可以根据 RendererAPI::GetAPI() 分支判断 new 哪一个
    RendererAPI* RenderCommand::s_RendererAPI = new OpenGLRendererAPI();

}