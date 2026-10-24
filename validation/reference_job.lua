local Runner=require('sim.gpu.warlock_runner')
local Job={};Job.__index=Job
function Job.new() return setmetatable({status='idle',runner=Runner.new()},Job) end
function Job:start(specializeFeatures)
 self.status='running'
 local ok,err=pcall(function()
  self.suite=require('data.validation.forever_baseline')
  for file,expected in pairs(self.suite.kernelHashes) do
   local hash=love.data.hash('string','sha256',assert(love.filesystem.read(file)))
   local hex=hash:gsub('.',function(c) return ('%02x'):format(c:byte()) end)
   assert(hex==expected,'Reference suite kernel/layout snapshot is stale: '..file)
  end
  local suiteHash=love.data.hash('string','sha256',assert(love.filesystem.read('data/validation/forever_baseline.json'))):gsub('.',function(c) return ('%02x'):format(c:byte()) end)
  assert(suiteHash==self.suite.suiteHash,'Reference suite JSON/Lua snapshots differ')
  local candidates={};for i,s in ipairs(self.suite.scenarios) do candidates[i]={id=s.id,packed=s.packed,duration=s.duration} end
  local started,reason=self.runner:start({schemaVersion=1,classId='warlock',mode='detailed',iterations=self.suite.iterations,seed=self.suite.seed,batchSize=256,specializeFeatures=specializeFeatures,candidates=candidates})
  assert(started,reason)
 end)
 if not ok then self.status='failed';self.message=tostring(err);self.runner:dispose() end
end
function Job:update()
 if self.status~='running' then return end
 self.runner:update()
 if self.runner.status=='failed' then self.status='failed';self.message=self.runner.error;return end
 if self.runner.status~='complete' then return end
 local ok,err=pcall(function()
  local result=self.runner.result
  result.suiteId=self.suite.suiteId;result.suiteHash=self.suite.suiteHash;result.kernelHashes=self.suite.kernelHashes
  result.iterations=self.suite.iterations;result.seed=self.suite.seed
  result.runtime=require('app.runtime').inspect(false)
  assert(love.filesystem.write('warlock-parity.json',require('app.json').encode(result)..'\n'),'Unable to write parity results')
  self.message='Diagnostic reference results written to '..love.filesystem.getSaveDirectory()..'/warlock-parity.json'
  self.status='complete';self.runner:dispose()
 end)
 if not ok then self.status='failed';self.message=tostring(err);self.runner:dispose() end
end
function Job:dispose() self.runner:dispose() end
return Job
