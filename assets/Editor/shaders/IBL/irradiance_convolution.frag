#version 410 core
out vec4 FragColor;

in vec3 v_LocalPos;

uniform samplerCube u_EnvironmentMap;

const float PI = 3.14159265359;

void main() {		
    // 我们当前正在烘焙的 Cubemap 面上的方向
    vec3 N = normalize(v_LocalPos);

    vec3 irradiance = vec3(0.0);   
    
    // 建立切线空间 (Tangent Space)
    vec3 up    = vec3(0.0, 1.0, 0.0);
    // 【新增】：如果法线接近 Y 轴，强行把 up 换成 Z 轴，防止叉乘等于 0！
    if (abs(N.y) > 0.999) {
        up = vec3(0.0, 0.0, 1.0);
    }
    vec3 right = normalize(cross(up, N));
    up         = normalize(cross(N, right));
       
    // 步长越小，烘焙越慢但越精确。0.025 是一个很好的平衡点
    float sampleDelta = 0.025;
    float nrSamples = 0.0;
    
    // 黎曼积分 (Riemann Sum) 遍历整个半球
    for(float phi = 0.0; phi < 2.0 * PI; phi += sampleDelta) {
        for(float theta = 0.0; theta < 0.5 * PI; theta += sampleDelta) {
            // 球面坐标转笛卡尔坐标
            vec3 tangentSample = vec3(sin(theta) * cos(phi),  sin(theta) * sin(phi), cos(theta));
            // 切线空间转世界空间
            vec3 sampleVec = tangentSample.x * right + tangentSample.y * up + tangentSample.z * N; 

            // 采样高分辨率的环境 HDR 贴图，并累加颜色
            irradiance += texture(u_EnvironmentMap, sampleVec).rgb * cos(theta) * sin(theta);
            nrSamples++;
        }
    }
    // 取平均值并乘以 PI
    irradiance = PI * irradiance * (1.0 / float(nrSamples));
    
    FragColor = vec4(irradiance, 1.0);
}