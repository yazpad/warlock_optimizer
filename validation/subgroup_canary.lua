-- Canary readback checks with synthetic words; real shader compilation is separate.
local observed=32
local released=0
local values={}
local function resource(t) t.release=function() released=released+1 end;return t end
love={graphics={},filesystem={},data={}}
love.graphics.fastsimComputeTiming=function() end
love.filesystem.read=function() return 'synthetic canary' end
love.graphics.newComputeShader=function(_,options) assert(options.requiredsubgroupsize==32);return resource({send=function() end}) end
love.graphics.newBuffer=function() return resource({}) end
love.graphics.dispatchThreadgroups=function() values={};for i=1,64 do values[i]=observed end end
love.graphics.readbackBufferAsync=function()
 return resource({wait=function() end,isComplete=function() return true end,hasError=function() return false end,getBufferData=function() return {getSize=function() return 256 end} end})
end
love.data.unpack=function(_,_,offset) return values[(offset-1)/4+1] end
local canary=require('validation.subgroup_gpu')
assert(canary.check(32)==32 and released==3)
observed=64
assert(not pcall(canary.check,32) and released==6,'Mismatched actual width must reject and release resources')
love.graphics.fastsimComputeTiming=nil
assert(not pcall(canary.check,32),'Unpatched runtime must reject explicit verification')
print('PASS: subgroup canary verifies observed width, rejects mismatch/unpatched runtime and releases resources (synthetic words)')
