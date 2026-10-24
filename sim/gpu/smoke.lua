-- Infrastructure validation only: this is not a combat benchmark.
local Smoke={}
Smoke.__index=Smoke
local function release(object) if object then object:release() end end
function Smoke.new()
 return setmetatable({status='idle',message='Compute check has not run.'},Smoke)
end
function Smoke:dispose()
 release(self.readback);release(self.input);release(self.output);release(self.shader)
 self.readback,self.input,self.output,self.shader=nil,nil,nil,nil
end
function Smoke:fail(message)
 self.status='failed';self.message=tostring(message);self:dispose()
end
function Smoke:start(count)
 if self.status=='running' then return false end
 self:dispose()
 count=count or 257 -- A partial final workgroup exercises bounds checks.
 assert(type(count)=='number' and count%1==0 and count>=1 and count<=65536,'Invalid compute check size')
 self.count=count;self.status='running';self.message='Checking compute buffers…';self.started=love.timer.getTime()
 local ok,err=pcall(function()
  local values={};for i=1,count do values[i]=i-1 end
  self.shader=love.graphics.newComputeShader(love.filesystem.read('sim/gpu/smoke.glsl'))
  self.input=love.graphics.newBuffer('uint32',values,{shaderstorage=true})
  self.output=love.graphics.newBuffer('uint32',count,{shaderstorage=true})
  assert(self.input:getElementStride()==4 and self.output:getElementStride()==4,'Unexpected uint32 buffer stride')
  self.shader:send('Inputs',self.input);self.shader:send('Outputs',self.output)
  love.graphics.dispatchThreadgroups(self.shader,math.ceil(count/64),1,1)
  self.readback=love.graphics.readbackBufferAsync(self.output)
 end)
 if not ok then self:fail(err) end
 return ok
end
function Smoke:update()
 if self.status~='running' then return end
 local ok,err=pcall(function()
  self.readback:update()
  if self.readback:hasError() then error('GPU readback failed') end
  if not self.readback:isComplete() then
   if love.timer.getTime()-self.started>15 then error('Compute check timed out after 15 seconds') end
   return
  end
  local data=self.readback:getBufferData()
  assert(data and data:getSize()==self.count*4,'Unexpected readback size')
  for i=1,self.count do
   local actual=love.data.unpack('I4',data,(i-1)*4+1)
   local expected=((i-1)*1664525+1013904223)%4294967296
   assert(actual==expected,('Compute mismatch at lane %d: %s vs %s'):format(i-1,tostring(actual),tostring(expected)))
  end
  self.elapsedMs=(love.timer.getTime()-self.started)*1000
  self.status='passed';self.message=('Compute check passed: %d lanes, %.2f ms including compilation/readback.'):format(self.count,self.elapsedMs)
  self:dispose()
 end)
 if not ok then self:fail(err) end
end
return Smoke
