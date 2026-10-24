#pragma language glsl4
layout(local_size_x=64) in;
layout(std430) readonly buffer Configs { uint configs[]; };
layout(std430) readonly buffer Parameters { uint parameters[]; };
layout(std430) writeonly buffer Results { uint results[]; };
void computemain() {
    uint lane=love_GlobalThreadID.x;
    if(lane>=parameters[4]) return;
    results[lane]=configs[0]+parameters[2]+parameters[3]+lane;
}
