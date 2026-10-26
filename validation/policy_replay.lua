-- Invoke with tools/run_mcts.lua arguments. This substitutes an APL-following
-- controller and checks all 16 production report words, not merely DPS.
local ffi = require('ffi')
local transport = require('tools.regret_transport')
local controller = require('tools.policy_controller')
local open = transport.open
local checked = 0
function transport.open(...)
  local worker = open(...)
  local dispatch = worker.dispatch
  local reference
  function worker:dispatch(count, config, n)
    local bytes = dispatch(self, count, config, n)
    assert(n == 1, 'APL replay regression must not run search')
    local words = ffi.new('uint32_t[?]', #config / 4)
    ffi.copy(words, config, #config)
    local report = ffi.new('uint32_t[32]')
    ffi.copy(report, bytes, 128)
    if words[229] == 0 then reference = bytes:sub(1,64) end
    if report[18] == 0 then
      assert(bytes:sub(1,64) == reference,
        'Forcing the APL recommendation changed the production report')
      checked = checked + 1
    end
    return bytes
  end
  return worker
end
function controller.fight(engine, seed, opts)
  local history = {}
  while true do
    local state = engine:observe(history, seed)
    assert(state.valid, 'Invalid forced APL history')
    if state.terminal then
      return {dps=state.dps, decisions=#history, searches=0, timeline={}}
    end
    assert(#history < opts.maxDecisions)
    history[#history+1] = state.aplAction
  end
end
dofile('tools/run_mcts.lua')
assert(checked >= 2, 'Expected at least two complete replay comparisons')
print('PASS: forced APL history matches every production report word')
