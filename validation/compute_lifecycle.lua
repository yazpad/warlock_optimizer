-- Test host-side async lifecycle and rejection paths. Does not execute a GPU.
local Smoke=require('sim.gpu.smoke')
local now,mode,releases=0,'ok',0
local outputCount=0
local function resource(t)
 t=t or {};t.release=function() releases=releases+1 end;return t
end
love={graphics={},filesystem={},timer={},data={}}
love.timer.getTime=function() return now end
love.filesystem.read=function() return 'shader source' end
love.data.unpack=function(_,data,position)
 local lane=(position-1)/4
 return (lane*1664525+1013904223+(mode=='corrupt' and 1 or 0))%4294967296
end
love.graphics.newComputeShader=function()
 if mode=='compile-error' then error('compile failed') end
 return resource({send=function() end})
end
love.graphics.newBuffer=function(_,values)
 if type(values)=='number' then outputCount=values end
 return resource({getElementStride=function() return 4 end})
end
love.graphics.dispatchThreadgroups=function(_,x) assert(x==math.ceil(outputCount/64)) end
love.graphics.readbackBufferAsync=function()
 return resource({update=function() end,hasError=function() return mode=='readback-error' end,
 isComplete=function() return mode~='waiting' end,
 getBufferData=function() return {getSize=function() return outputCount*4 end} end})
end
for _,count in ipairs({1,64,65,257}) do
 mode='ok';local job=Smoke.new();assert(job:start(count));assert(not job:start(count));job:update()
 assert(job.status=='passed',job.message);assert(job.output==nil,'completed job must release GPU resources')
end
for _,failure in ipairs({'corrupt','readback-error','compile-error','waiting'}) do
 mode=failure;now=0;local job=Smoke.new();job:start(257);now=16;job:update()
 assert(job.status=='failed',failure..' must fail');assert(job.input==nil and job.readback==nil,'failed job must release resources')
 mode='ok';assert(job:start(257));job:update();assert(job.status=='passed','failed jobs must be restartable')
end
assert(not pcall(function() Smoke.new():start(0) end))
assert(releases>0)
print('PASS: async host lifecycle, partial groups, corrupt reports, readback errors, timeout, cleanup and retry (mock GPU)')
