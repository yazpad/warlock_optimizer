export function getTalentFlagsFromRanks(talentTreeRanks = {}) {
  const ranks = {};
  for (const [tree, talents] of Object.entries(talentTreeRanks || {})) {
    if (talents && typeof talents === 'object') {
      for (const [talent, value] of Object.entries(talents)) {
        const cleanTree = tree.toLowerCase();
        const cleanTalent = talent.toLowerCase().replace(/_/g, ' ');
        ranks[`${cleanTree}.${cleanTalent}`] = Number(value) || 0;
      }
    } else {
      const cleanKey = tree.toLowerCase().replace(/_/g, ' ');
      ranks[cleanKey] = Number(talents) || 0;
    }
  }
  const rank = (tree, talent) => ranks[`${tree}.${talent}`] || 0;
  const aff = name => rank('affliction', name);
  const demo = name => rank('demonology', name);
  const destro = name => rank('destruction', name);
  const improvedCorruption = aff('improved corruption');
  const improvedDrains = aff('improved drains');
  const agonizingFlames = destro('agonizing flames');
  const shadowMastery = aff('shadow mastery');
  const improvedImp = demo('improved imp');
  const unholyPower = demo('unholy power');
  const improvedSayaad = demo('improved sayaad');
  const masterDemo = demo('master demonologist');
  const soulLink = demo('soul link') > 0;
  const improvedShadowBolt = destro('improved shadow bolt');
  const sacRank = demo('demonic sacrifice');
  const aftermath = destro('aftermath');
  const cataclysm = destro('cataclysm');
  const cataclysmCostMult = cataclysm === 3 ? 0.90 : cataclysm === 2 ? 0.94 : cataclysm === 1 ? 0.97 : 1.0;
  const affBonus = agonizingFlames >= 3 ? 0.10 : agonizingFlames === 2 ? 0.07 : agonizingFlames === 1 ? 0.03 : 0;

  const improvedAgony = aff('improved bane of agony') || aff('improved curse of agony');
  const amplifyCurse = aff('amplify curse') > 0;
  const felVitality = demo('fel vitality');
  const demonicPact = demo('demonic pact') > 0;

  return {
    // Direct flags and rank-scaled values consumed by the current DES shader.
    improvedTap: aff('improved life tap') > 0,
    tapBonus: aff('improved life tap') * 0.10,
    suppressionHit: aff('suppression'),
    instantCorruption: improvedCorruption >= 5,
    improvedCorruptionBonus: improvedCorruption >= 5 ? 0.10 : improvedCorruption * 0.02,
    improvedAgony: improvedAgony > 0,
    improvedAgonyRank: improvedAgony,
    improvedAgonyBonus: improvedAgony * 0.05,
    amplifyCurse,
    felVitality: felVitality > 0,
    felVitalityRank: felVitality,
    felVitalityBonus: felVitality * 0.05,
    maledictionBonus: 0,
    afflictionRangeMultiplier: 1 + aff('grim reach') * 0.10,
    destructionRangeMultiplier: 1 + destro('destructive reach') * 0.10,
    improvedDrainsBonus: improvedDrains >= 3 ? 0.20 : improvedDrains === 2 ? 0.13 : improvedDrains === 1 ? 0.07 : 0,
    malevolence: aff('malevolence'),
    nightfall: aff('nightfall') > 0,
    nightfallChance: aff('nightfall') * 0.02,
    isb: improvedShadowBolt > 0,
    isbBonus: improvedShadowBolt * 0.04,
    ruin: destro('ruin') > 0,
    ruinRank: destro('ruin'),
    shadowburn: destro('shadowburn') > 0,
    siphonLife: aff('siphon life') > 0,
    soulSiphonBonus: aff('soul siphon') * 0.04,
    decimation: demo('decimation') > 0,
    decimationRank: demo('decimation'),
    baneRank: destro('bane'),
    demonicBrand: demo('demonic brand') > 0,
    demonicBrandRank: demo('demonic brand'),
    demonicEnergies: demo('demonic energies'),
    demonicKnowledge: demo('demonic knowledge'),
    demonicPact,
    felDomination: demo('fel domination') > 0,
    masterSummoner: demo('master summoner'),
    masterDemo,
    soulLink,
    sacRank,
    conflagrate: destro('conflagrate') > 0,
    incinerate: destro('incinerate') > 0,
    wrack: aff('wrack') > 0,
    aftermath: aftermath > 0,
    aftermathRank: aftermath,
    aftermathBonus: aftermath * 0.10,
    cataclysm: cataclysm > 0,
    cataclysmRank: cataclysm,
    cataclysmCostMult,
    afBonus: affBonus,
    fnbCrit: [0, 8, 17, 25][destro('fire and brimstone')] || 0,
    snfChance: destro('shadow and flame') * 0.20,
    snfBonus: destro('shadow and flame') * 0.02,
    dotCrit: 1.0 + 0.50 * (1.0 + aff('pandemic') / 3),
    shadowMasteryBonus: shadowMastery * 0.02,
    shadowMultiplier: 1,
    fireMultiplier: 1,
    corrMultiplier: 0,
    // These are separate source damage multipliers, so Improved Imp stacks
    // multiplicatively with Unholy Power rather than additively.
    petFireboltMult: (1 + unholyPower * 0.02) * (1 + improvedImp * 0.10) * (1 + masterDemo * 0.02) * (soulLink ? 1.03 : 1),
    petMeleeMult: (1 + unholyPower * 0.02) * (soulLink ? 1.03 : 1),
    petLashMult: (1 + unholyPower * 0.02) * (1 + improvedSayaad * 0.10) * (1 + masterDemo * 0.02) * (soulLink ? 1.03 : 1),
    brandMult: (1 + unholyPower * 0.02) * (1 + masterDemo * 0.02) * (soulLink ? 1.03 : 1),
  };
}

