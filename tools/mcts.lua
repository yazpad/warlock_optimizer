-- tools/mcts.lua
-- Diagnostic open-loop PUCT with root rollout warmup and an APL root prior.
-- Built entirely on top of the persistent regret worker so zero main simulator code is touched.
local bit = require('bit')
local regret = require('tools.regret')

local M = {}

--- Create a new MCTS node.
-- @param plan Table array of action IDs representing the sequence from root.
-- @param mask Bitmask of legal actions available at this node.
-- @param prior Float prior probability (e.g. from APL recommendation).
-- @param takenAction The action taken to reach this node.
function M.createNode(plan, mask, prior, takenAction)
  return {
    plan = plan or {},
    mask = mask or 0,
    prior = prior or 1.0,
    action = takenAction,
    children = {},
    visits = 0,
    totalValue = 0,
    mean = 0,
    isExpanded = false,
    rejected = false
  }
end

--- Initialize child nodes using an optional APL prior.
-- Gives a higher prior mass (e.g. 0.7) to the APL-recommended action,
-- and spreads the remaining mass uniformly across other legal actions.
-- @param node The parent node to expand.
-- @param aplAction Optional canonical action recommended by APL at this state.
-- @param aplWeight Prior probability mass to allocate to the APL action (default 0.70).
function M.expand(node, aplAction, aplWeight)
  if node.isExpanded or node.mask == 0 then return end
  aplWeight = aplWeight or 0.70
  local legal = regret.actions(node.mask)
  if #legal == 0 then
    node.isExpanded = true
    return
  end

  local hasApl = false
  if aplAction then
    for _, a in ipairs(legal) do
      if a == aplAction then hasApl = true; break end
    end
  end

  local altCount = hasApl and (#legal - 1) or #legal
  local aplProb = hasApl and (altCount > 0 and aplWeight or 1.0) or (1.0 / #legal)
  local altProb = (hasApl and altCount > 0) and ((1.0 - aplProb) / altCount) or (1.0 / #legal)

  for _, a in ipairs(legal) do
    local childPlan = { unpack(node.plan) }
    childPlan[#childPlan + 1] = a
    local prior = (hasApl and a == aplAction) and aplProb or altProb
    node.children[a] = M.createNode(childPlan, 0, prior, a)
  end
  node.isExpanded = true
end

--- Select best child using PUCT formula:
-- Q is expressed in units of 100 DPS so exploration is useful at DPS scale.
-- UCT(s, a) = Q(s, a) / 100 + c_puct * P(s, a) * sqrt(sum_b N(s, b)) / (1 + N(s, a))
function M.selectChild(node, cPuct, qBaseline)
  cPuct = cPuct or 2.0
  local bestScore = -math.huge
  local bestChild = nil

  -- Calculate total visits among legal valid children
  local totalN = 0
  for _, child in pairs(node.children) do
    if not child.rejected then
      totalN = totalN + child.visits + (child.pending or 0)
    end
  end
  local sqrtN = math.sqrt(math.max(1, totalN))

  for _, child in pairs(node.children) do
    if not child.rejected then
      -- If child hasn't been visited yet, use parent/baseline mean as default Q
      local q = (child.visits > 0) and child.mean or (qBaseline or node.mean)
      local u = cPuct * child.prior * (sqrtN / (1 + child.visits + (child.pending or 0)))
      local score = q / 100.0 + u
      if score > bestScore then
        bestScore = score
        bestChild = child
      end
    end
  end
  return bestChild
end

--- Backpropagate terminal evaluation return up the traversed search path.
function M.backpropagate(path, reward)
  for i = #path, 1, -1 do
    local node = path[i]
    node.visits = node.visits + 1
    node.totalValue = node.totalValue + reward
    node.mean = node.totalValue / node.visits
  end
end

--- Batched PUCT Search over an action decision point.
-- Explores up to `depth` actions ahead, evaluates with GPU batch rollouts,
-- where the GPU forces the action prefix and lets APL roll out to terminal fight end.
-- @param evalBatchFunc function(plans, sampleCount, sampleOffset) -> results
-- @param rootMask Legal action mask at the decision state.
-- @param aplAction Canonical APL recommendation at the root state.
-- @param opts Table of search hyperparameters:
--             depth: max action prefix depth (default: 4)
--             rounds: number of MCTS iterations/batches (default: 32)
--             batchSize: parallel plans per GPU dispatch (default: 16)
--             samples: rollouts per plan (default: 8)
--             cPuct: PUCT exploration weight (default: 2.0)
--             aplWeight: Root prior on baseline APL action (default: 0.70)
function M.search(evalBatchFunc, rootMask, aplAction, opts)
  opts = opts or {}
  local depth = opts.depth or 4
  local rounds = opts.rounds or 32
  local batchSize = opts.batchSize or 16
  local samples = opts.samples or 8
  local cPuct = opts.cPuct or 2.0
  local aplWeight = opts.aplWeight or 0.70

  local root = M.createNode({}, rootMask, 1.0, nil)
  M.expand(root, aplAction, aplWeight)

  -- Evaluate every root alternative before PUCT so its initial Q is measured.
  local warmPlans, warmChildren = {}, {}
  for _, action in ipairs(regret.actions(rootMask)) do
    warmPlans[#warmPlans + 1] = root.children[action].plan
    warmChildren[#warmChildren + 1] = root.children[action]
  end
  if #warmPlans > 0 then
    local results = evalBatchFunc(warmPlans, samples, 0)
    for i, child in ipairs(warmChildren) do
      local res = assert(results[i], 'Missing warmup result')
      if res.valid then
        child.mask = res.mask
        child.evaluated = true
        M.backpropagate({root, child}, res.mean)
      else child.rejected = true end
    end
  end

  for round = 1, rounds do
    local batchPlans = {}
    local batchPaths = {}
    local reservedPaths, selectedPlans = {}, {}

    -- Select `batchSize` leaves via PUCT
    for b = 1, batchSize do
      local node = root
      local path = { root }

      while #node.plan < depth do
        if not node.isExpanded then
          -- Unexpanded node with known mask
          if node.mask ~= 0 then
            M.expand(node, nil, aplWeight)
          else
            break -- Leaf needs evaluation to know mask
          end
        end

        local child = M.selectChild(node, cPuct, root.mean)
        if not child then break end
        node = child
        path[#path + 1] = node
      end

      if #node.plan > 0 and not node.rejected then
        for _, ancestor in ipairs(path) do ancestor.pending = (ancestor.pending or 0) + 1 end
        reservedPaths[#reservedPaths + 1] = path
        local key = table.concat(node.plan, ',')
        if not selectedPlans[key] then
          selectedPlans[key] = true
          batchPlans[#batchPlans + 1] = node.plan
          batchPaths[#batchPaths + 1] = path
        end
      end
    end

    if #batchPlans == 0 then break end

    -- Parallel GPU rollout evaluation for all plans in this batch
    local results = evalBatchFunc(batchPlans, samples, round * 1000)

    assert(#results == #batchPlans, 'Missing search results')
    for _, path in ipairs(reservedPaths) do
      for _, ancestor in ipairs(path) do ancestor.pending = ancestor.pending - 1 end
    end
    for i, res in ipairs(results) do
      local path = batchPaths[i]
      local leaf = path[#path]

      if res.valid then
        leaf.mask = leaf.evaluated and bit.band(leaf.mask, res.mask) or res.mask
        leaf.evaluated = true
        M.backpropagate(path, res.mean)
      else
        leaf.rejected = true
        leaf.mask = 0
        -- An illegal stochastic prefix is not a negative reward for its ancestors.
      end
    end
  end

  -- Aggregate best results per root alternative action
  local bestPerAction = {}
  for action, child in pairs(root.children) do
    if not child.rejected and child.visits > 0 then
      -- Find the highest-mean subnode under this child
      local bestNode = child
      local function findBestLeaf(curr)
        if curr.visits > 0 and curr.mean > bestNode.mean then
          bestNode = curr
        end
        for _, c in pairs(curr.children) do
          if not c.rejected and c.visits > 0 then findBestLeaf(c) end
        end
      end
      findBestLeaf(child)

      bestPerAction[action] = {
        action = action,
        name = regret.names[action] or ('Action ' .. action),
        plan = bestNode.plan,
        visits = child.visits,
        meanDps = child.mean,
        bestLeafMean = bestNode.mean,
        prior = child.prior
      }
    end
  end

  return bestPerAction, root
end

return M
