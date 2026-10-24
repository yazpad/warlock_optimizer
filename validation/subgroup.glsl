#pragma language glsl4
#extension GL_KHR_shader_subgroup_basic : require
layout(local_size_x=64) in;
layout(std430) writeonly buffer Results { uint observedWidths[]; };
void computemain() { observedWidths[love_GlobalThreadID.x]=gl_SubgroupSize; }
