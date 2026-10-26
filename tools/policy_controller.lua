-- Receding-horizon controller: only commit the first action, then observe its own next state.
local mcts = require('tools.mcts')
local regret = require('tools.regret')
local M = {}
function M.fight(engine, fightSeed, opts)
  local history, timeline, searches = {}, {}, 0
  while true do
    local state = engine:observe(history, fightSeed)
    assert(state.valid, 'Invalid controller trajectory')
    if state.terminal then
      assert(state.decisions == #history, 'Incomplete committed trajectory')
      return {dps=state.dps, decisions=#history, searches=searches, timeline=timeline, history=history}
    end
    assert(#history < opts.maxDecisions, 'Policy decision capacity exceeded; refusing partial-policy results')
    local actions = regret.actions(state.mask)
    local chosen = actions[1] or 0 -- Zero delegates unsupported decisions to the APL.
    local decisionEvidence
    if #actions > 1 then
      local function evaluate(plans, samples, offset)
        -- Search seeds are disjoint from live evaluation seeds and never inspect live future returns.
        return engine:evaluate(history, fightSeed, state, plans, samples,
          1000000 + #history * 262144 + offset)
      end
      local alternatives = mcts.search(evaluate, state.mask, state.aplAction, opts)
      -- Tree means mix exploratory continuations. Compare discovered plans
      -- and one-action APL rollouts on fresh common seeds before committing.
      local plans, seen = {{state.aplAction}}, {[tostring(state.aplAction)]=true}
      local function add(plan)
        local key=table.concat(plan, ',')
        if not seen[key] then seen[key]=true; plans[#plans+1]=plan end
      end
      for _, action in ipairs(actions) do
        local candidate = alternatives[action]
        add({action})
        if candidate then add(candidate.plan) end
      end
      local samples=math.max(32, opts.samples or 8)
      -- Above the largest tree offset (256000), within this decision's seed block.
      local validated=evaluate(plans,samples,261000)
      local baseline=assert(validated[1], 'Missing APL validation')
      assert(baseline.valid, 'Invalid APL validation')
      local bestMean=baseline.mean
      chosen=state.aplAction
      decisionEvidence={samples=samples,guardStandardErrors=4,selectedPlan=plans[1],alternatives={}}
      for i,result in ipairs(validated) do
        if result.valid then
          local delta,m2=0,0
          assert(result.values and baseline.values and #result.values==samples and #baseline.values==samples,
            'Paired validation requires individual rollout values')
          for j=1,samples do
            local d=result.values[j]-baseline.values[j]
            local change=d-delta;delta=delta+change/j;m2=m2+change*(d-delta)
          end
          local se=math.sqrt(math.max(0,m2)/(samples-1)/samples)
          decisionEvidence.alternatives[#decisionEvidence.alternatives+1]={plan=plans[i],meanDps=result.mean,
            gainDps=delta,standardError=se}
          -- Many alternatives are compared: require a substantial noise margin.
          -- This is a local guard, not a policy-level confidence claim.
          if delta>4*se and result.mean>bestMean then
            chosen=plans[i][1];bestMean=result.mean
            decisionEvidence.selectedPlan=plans[i]
          end
        end
      end
      searches = searches + 1
    end
    timeline[#timeline+1] = {ordinal=#history, time=state.time, mana=state.mana,
      totalDamage=state.totalDamage, action=chosen, actionName=regret.names[chosen],
      aplAction=state.aplAction, aplActionName=regret.names[state.aplAction], signature=state.signature,
      searchValidation=decisionEvidence}
    history[#history+1] = chosen
    if engine.progress then engine:progress(#history, state) end
  end
end
function M.summarize(rows)
  assert(#rows >= 2, 'At least two complete fight pairs required')
  local baseline, policy, mean, m2, wins, decisions, searches = 0, 0, 0, 0, 0, 0, 0
  for i, row in ipairs(rows) do
    local delta = row.policyDps - row.baselineDps
    row.dpsDelta = delta
    baseline = baseline + row.baselineDps / #rows
    policy = policy + row.policyDps / #rows
    local change = delta - mean; mean = mean + change / i; m2 = m2 + change * (delta - mean)
    if delta > 0 then wins = wins + 1 end
    decisions = decisions + row.decisions; searches = searches + row.searches
  end
  local se = math.sqrt(math.max(0, m2) / (#rows-1) / #rows)
  -- Rounded 97.5% Student-t critical values; cap df at 30 conservatively for larger runs.
  local critical = {12.707,4.303,3.183,2.777,2.571,2.447,2.365,2.307,2.263,2.229,
    2.201,2.179,2.161,2.145,2.132,2.120,2.110,2.101,2.094,2.086,
    2.080,2.074,2.069,2.064,2.060,2.056,2.052,2.049,2.046,2.043}
  local margin = critical[math.min(#rows-1,30)] * se
  return {ciLow=mean-margin,ciHigh=mean+margin,confidenceLevel=0.95,
    confidenceMethod='paired Student-t, conservative df cap 30',meanBaselineDps=baseline, meanAlgoDps=policy, dpsDelta=mean,
    dpsDeltaPct=baseline > 0 and mean * 100 / baseline or 0, standardError=se,
    winRatePct=wins * 100 / #rows, completedFightPairs=#rows, totalDecisions=decisions,
    totalSearches=searches}
end
return M
