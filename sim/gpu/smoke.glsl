#pragma language glsl4
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
layout(std430) readonly buffer Inputs { uint inputs[]; };
layout(std430) writeonly buffer Outputs { uint outputs[]; };
void computemain() {
    uint lane = love_GlobalThreadID.x;
    if (lane >= uint(inputs.length())) return;
    outputs[lane] = inputs[lane] * 1664525u + 1013904223u;
}
