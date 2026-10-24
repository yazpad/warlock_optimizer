-- Read gl_SubgroupSize back from the actual compute pipeline; no inferred width.
local M={}
function M.check(required)
 assert(love.graphics.fastsimComputeTiming,'Subgroup verification requires the patched LÖVE runtime')
 local shader,buffer,readback
 local ok,width=xpcall(function()
  shader=love.graphics.newComputeShader(assert(love.filesystem.read('validation/subgroup.glsl')),{requiredsubgroupsize=required})
  buffer=love.graphics.newBuffer('uint32',64,{shaderstorage=true})
  shader:send('Results',buffer)
  love.graphics.dispatchThreadgroups(shader,1,1,1)
  readback=love.graphics.readbackBufferAsync(buffer,0,64*4);readback:wait()
  assert(readback:isComplete() and not readback:hasError(),'Subgroup canary readback failed')
  local bytes=readback:getBufferData();assert(bytes:getSize()==64*4,'Subgroup canary size mismatch')
  local observed
  for lane=0,63 do
   local value=love.data.unpack('I4',bytes,lane*4+1)
   observed=observed or value
   assert(value==observed and (not required or value==required),'Actual subgroup width differs from request')
  end
  return observed
 end,debug.traceback)
 if readback then readback:release() end
 if buffer then buffer:release() end
 if shader then shader:release() end
 assert(ok,width)
 return width
end
return M
