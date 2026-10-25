import { isAPLRuleEnabled, isAPLRuleBlocking, getAPLUnreachableFlags } from './apl_rules.js';

// Authentic Action Priority List (APL) Engine & Interactive Manager
import { APL_ACTION, APL_COND } from './model.js';

export const ACTION_INFO = {
  elements: { id: 'elements', spell: 'Curse of the Elements', icon: 'Spell_Shadow_ChillTouch.png', action: APL_ACTION.CURSE_OF_ELEMENTS, defaultCond: '!debuff.assigned_curse' },
  spellblasting: { id: 'spellblasting', spell: 'Major Spellblasting Potion', icon: 'INV_Potion_51.png', action: APL_ACTION.SPELLBLASTING, defaultCond: 'true' },
  amplify: { id: 'amplify', spell: 'Amplify Curse', icon: 'Spell_Shadow_Contagion.png', action: APL_ACTION.AMPLIFY_CURSE, defaultCond: '!buff.amplify_curse' },
  petSwap: { id: 'petSwap', spell: 'Sacrifice Succubus + Fel Domination: Summon Imp', icon: 'Spell_Shadow_SummonImp.png', action: APL_ACTION.SACRIFICE_SUCCUBUS_SUMMON_IMP, defaultCond: 'true' },

  berserking: { id: 'berserking', spell: 'Berserking', icon: 'Racial_Troll_Berserk.png', action: APL_ACTION.BERSERKING, defaultCond: 'true' },
  bloodFury: { id: 'bloodFury', spell: 'Blood Fury', icon: 'Racial_Orc_BerserkerStrength.png', action: APL_ACTION.BLOOD_FURY, defaultCond: 'true' },
  eureka: { id: 'eureka', spell: 'Eureka!', icon: 'Spell_Nature_WispSplode.png', action: APL_ACTION.EUREKA, defaultCond: 'true' },
  tap: { id: 'tap', spell: 'Life Tap', icon: 'Spell_Shadow_BurningSpirit.png', action: APL_ACTION.LIFE_TAP, defaultCond: 'mana_pct <= 25' },
  nightfall: { id: 'nightfall', spell: 'Nightfall: Shadow Bolt', icon: 'Spell_Shadow_Twilight.png', action: APL_ACTION.NIGHTFALL_SHADOW_BOLT, defaultCond: 'buff.shadow_trance' },
  brand: { id: 'brand', spell: 'Demonic Brand', icon: 'ability_demonhunter_chaoticimprint_fire.png', action: APL_ACTION.DEMONIC_BRAND_SEARING_PAIN, defaultCond: 'debuff.demonic_brand_missing' },
  decimateSearing: { id: 'decimateSearing', spell: 'Decimation: Searing Pain', icon: 'Spell_Fire_SoulBurn.png', action: APL_ACTION.DECIMATION_SEARING_PAIN, defaultCond: 'decimation.inactive' },
  decimateSoulFire: { id: 'decimateSoulFire', spell: 'Decimation: Soul Fire', icon: 'Spell_Fire_Fireball.png', action: APL_ACTION.DECIMATION_SOUL_FIRE, defaultCond: 'decimation.active' },
  curse: { id: 'curse', spell: 'Bane of Doom', icon: 'Spell_Shadow_AuraOfDarkness.png', action: APL_ACTION.CURSE_OF_DOOM, defaultCond: 'target_ttd >= 60' },
  agony: { id: 'agony', spell: 'Bane of Agony', icon: 'Spell_Shadow_CurseOfSargeras.png', action: APL_ACTION.CURSE_OF_AGONY, defaultCond: 'target.debuff_remains("Bane of Agony") <= 0' },
  corr: { id: 'corr', spell: 'Corruption', icon: 'Spell_Shadow_AbominationExplosion.png', action: APL_ACTION.CORRUPTION, defaultCond: 'target.debuff_remains("Corruption") <= 0' },
  immo: { id: 'immo', spell: 'Immolate', icon: 'Spell_Fire_Immolation.png', action: APL_ACTION.IMMOLATE, defaultCond: 'target.debuff_remains("Immolate") <= 0' },
  conflag: { id: 'conflag', spell: 'Conflagrate', icon: 'Spell_Fire_Fireball.png', action: APL_ACTION.CONFLAGRATE, defaultCond: 'true' },
  shadowburn: { id: 'shadowburn', spell: 'Shadowburn', icon: 'Spell_Shadow_ScourgeBuild.png', action: APL_ACTION.SHADOWBURN, defaultCond: 'true' },
  incinerate: { id: 'incinerate', spell: 'Incinerate', icon: 'Spell_Fire_Burnout.png', action: APL_ACTION.INCINERATE_FILLER, defaultCond: 'true' },
  searing: { id: 'searing', spell: 'Searing Pain', icon: 'Spell_Fire_SoulBurn.png', action: APL_ACTION.SEARING_PAIN_FILLER, defaultCond: 'true' },
  drain: { id: 'drain', spell: 'Drain Soul', icon: 'ability_deathknight_hemorrhagicfever.png', action: APL_ACTION.DRAIN_SOUL_FILLER, defaultCond: 'true' },
  siphon: { id: 'siphon', spell: 'Siphon Life', icon: 'Spell_Shadow_Requiem.png', action: APL_ACTION.SIPHON_LIFE, defaultCond: 'target.debuff_remains("Siphon Life") <= 0' },
  wrack: { id: 'wrack', spell: 'Wrack', icon: 'ability_deathknight_hemorrhagicfever.png', action: APL_ACTION.DRAIN_HOPE, defaultCond: 'true' },
  hellfire: { id: 'hellfire', spell: 'Hellfire', icon: 'Spell_Fire_Incinerate.png', action: APL_ACTION.HELLFIRE, defaultCond: 'true' },
  bolt: { id: 'bolt', spell: 'Shadow Bolt', icon: 'Spell_Shadow_ShadowBolt.png', action: APL_ACTION.SHADOW_BOLT_FILLER, defaultCond: 'true' },
};

export const CANONICAL_ACTION_NAMES = {
  amplify: 'amplify_curse',
  petSwap: 'sacrifice_succubus_summon_imp',
  berserking: 'berserking',
  bloodFury: 'blood_fury',
  eureka: 'eureka',
  curse: 'bane_of_doom',
  agony: 'bane_of_agony',
  bolt: 'shadow_bolt',
  searing: 'searing_pain',
  tap: 'life_tap',
  corr: 'corruption',
  immo: 'immolate',
  conflag: 'conflagrate',
  shadowburn: 'shadowburn',
  incinerate: 'incinerate',
  nightfall: 'nightfall',
  drain: 'drain_soul',
  siphon: 'siphon_life',
  wrack: 'wrack',
  hellfire: 'hellfire',
  brand: 'demonic_brand',
  decimateSearing: 'decimate_searing',
  decimateSoulFire: 'decimate_soul_fire',
};

export const ACTION_ALIASES = {
  elements: 'elements', 'curse of the elements': 'elements', spellblasting: 'spellblasting', 'major spellblasting potion': 'spellblasting',
  amplify_curse: 'amplify', 'amplify curse': 'amplify', amplifycurse: 'amplify', amplify: 'amplify',
  sacrifice_succubus_summon_imp: 'petSwap', petswap: 'petSwap',
  berserking: 'berserking', beserking: 'berserking',
  blood_fury: 'bloodFury', bloodfury: 'bloodFury', 'blood fury': 'bloodFury',
  eureka: 'eureka', 'eureka!': 'eureka',
  // Shadow Bolt
  shadow_bolt: 'bolt', shadowbolt: 'bolt', 'shadow bolt': 'bolt', bolt: 'bolt',
  // Life Tap
  life_tap: 'tap', lifetap: 'tap', 'life tap': 'tap', tap: 'tap',
  // Bane of Doom
  bane_of_doom: 'curse', 'bane of doom': 'curse', curse_of_doom: 'curse', 'curse of doom': 'curse', doom: 'curse', curse: 'curse',
  // Bane of Agony
  bane_of_agony: 'agony', 'bane of agony': 'agony', curse_of_agony: 'agony', 'curse of agony': 'agony', agony: 'agony',
  // Searing Pain
  searing_pain: 'searing', 'searing pain': 'searing', searing: 'searing',
  // Corruption
  corruption: 'corr', corr: 'corr',
  // Immolate
  immolate: 'immo', immo: 'immo',
  // Conflagrate
  conflagrate: 'conflag', conflag: 'conflag',
  // Shadowburn
  shadowburn: 'shadowburn',
  // Incinerate
  incinerate: 'incinerate',
  // Nightfall
  nightfall: 'nightfall', 'nightfall: shadow bolt': 'nightfall', nightfall_bolt: 'nightfall', 'nightfall shadow bolt': 'nightfall', 'nightfall shadowbolt': 'nightfall',
  // Siphon Life
  siphon_life: 'siphon', 'siphon life': 'siphon', siphon: 'siphon',
  // Wrack
  wrack: 'wrack',
  // Drain Soul
  drain_soul: 'drain', 'drain soul': 'drain', drain: 'drain',
  // Demonic Brand
  demonic_brand: 'brand', 'demonic brand': 'brand', brand: 'brand', demonic_brand_refresher: 'brand', 'demonic brand refresher': 'brand',
  // Decimation Searing Pain
  decimate_searing: 'decimateSearing', 'decimate searing': 'decimateSearing', decimate_searing_pain: 'decimateSearing', 'decimation: searing pain': 'decimateSearing', 'decimation searing pain': 'decimateSearing', decimatesearing: 'decimateSearing',
  // Decimation Soul Fire
  decimate_soul_fire: 'decimateSoulFire', 'decimate soul fire': 'decimateSoulFire', decimate_soulfire: 'decimateSoulFire', 'decimation: soul fire': 'decimateSoulFire', 'decimation soul fire': 'decimateSoulFire', decimatesoulfire: 'decimateSoulFire',
  // Hellfire
  hellfire: 'hellfire',
};

export const DEFAULT_APL = [
  { ...ACTION_INFO.berserking, condition: 'On cooldown (Troll only)', rawCond: 'true', enabled: true },
  { ...ACTION_INFO.bloodFury, condition: 'On cooldown (Orc only)', rawCond: 'true', enabled: true },
  { ...ACTION_INFO.eureka, condition: 'On cooldown (Gnome only)', rawCond: 'true', enabled: true },
  { id: 'tap', spell: 'Life Tap', icon: 'Spell_Shadow_BurningSpirit.png', condition: 'Mana < 20%', rawCond: 'mana_pct < 20', enabled: true },
  { id: 'nightfall', spell: 'Nightfall: Shadow Bolt', icon: 'Spell_Shadow_Twilight.png', condition: 'Shadow Trance active', rawCond: 'buff.shadow_trance', enabled: true },
  { id: 'brand', spell: 'Demonic Brand Refresher', icon: 'ability_demonhunter_chaoticimprint_fire.png', condition: 'Brand missing', rawCond: 'debuff.demonic_brand_missing', enabled: false },
  { id: 'decimateSearing', spell: 'Decimation: Searing Pain', icon: 'Spell_Fire_SoulBurn.png', condition: 'Target HP < 35%, buff inactive', rawCond: 'decimation.inactive', enabled: false },
  { id: 'decimateSoulFire', spell: 'Decimation: Soul Fire', icon: 'Spell_Fire_Fireball.png', condition: 'Target HP < 35%, buff active', rawCond: 'decimation.active', enabled: false },
  { id: 'curse', spell: 'Bane of Doom', icon: 'Spell_Shadow_AuraOfDarkness.png', condition: 'Target TTDie >= 60s', rawCond: 'target_ttd >= 60 && !target.has_debuff("Bane of Doom")', enabled: true },
  { ...ACTION_INFO.amplify, condition: 'Before Agony', rawCond: 'target_ttd >= 20 && !target.has_debuff("Bane of Doom") && !target.has_debuff("Bane of Agony")', enabled: true },
  { id: 'agony', spell: 'Bane of Agony', icon: 'Spell_Shadow_CurseOfSargeras.png', condition: 'Target TTDie >= 20s & No Doom', rawCond: 'target_ttd >= 20 && !target.has_debuff("Bane of Doom") && !target.has_debuff("Bane of Agony")', enabled: true },
  { id: 'corr', spell: 'Corruption', icon: 'Spell_Shadow_AbominationExplosion.png', condition: 'Target TTDie >= 12s & !Active', rawCond: 'target_ttd >= 12 && !target.has_debuff("Corruption")', enabled: true },
  { id: 'immo', spell: 'Immolate', icon: 'Spell_Fire_Immolation.png', condition: 'Target TTDie >= 15s & !Active', rawCond: 'target_ttd >= 15 && !target.has_debuff("Immolate")', enabled: false },
  { id: 'conflag', spell: 'Conflagrate', icon: 'Spell_Fire_Fireball.png', condition: 'Immolate Remaining < 6s', rawCond: 'target.debuff_remains("Immolate") < 6', enabled: false },
  { id: 'shadowburn', spell: 'Shadowburn', icon: 'Spell_Shadow_ScourgeBuild.png', condition: 'Always when available', rawCond: 'true', enabled: false },
  { id: 'incinerate', spell: 'Incinerate', icon: 'Spell_Fire_Burnout.png', condition: 'Always', rawCond: 'true', enabled: false },
  { id: 'searing', spell: 'Searing Pain', icon: 'Spell_Fire_SoulBurn.png', condition: 'Always', rawCond: 'true', enabled: false },
  { id: 'drain', spell: 'Wrack', icon: 'ability_deathknight_hemorrhagicfever.png', condition: 'Always', rawCond: 'true', enabled: false },
  { id: 'siphon', spell: 'Siphon Life', icon: 'Spell_Shadow_Requiem.png', condition: 'Missing Siphon Life', rawCond: 'target.debuff_remains("Siphon Life") <= 0', enabled: false },
  { id: 'wrack', spell: 'Wrack', icon: 'ability_deathknight_hemorrhagicfever.png', condition: 'Always', rawCond: 'true', enabled: false },
  { id: 'hellfire', spell: 'Hellfire', icon: 'Spell_Fire_Incinerate.png', condition: 'Always', rawCond: 'true', enabled: false },
  { id: 'bolt', spell: 'Shadow Bolt', icon: 'Spell_Shadow_ShadowBolt.png', condition: 'Always', rawCond: 'true', enabled: true }
];

export function normalizeConditionForAction(rawInput, actionId) {
  let raw = String(rawInput ?? '').trim();
  if (!raw || raw === 'true' || raw === 'always' || raw === '1') return 'true';
  if (raw === 'false' || raw === 'never' || raw === '0') return 'false';

  // Handle boolean and (case insensitive)
  if (/\s+(?:and|&&)\s+/i.test(raw)) {
    const parts = raw.split(/\s+(?:and|&&)\s+/i);
    return parts.map(p => normalizeConditionForAction(p, actionId)).join(' && ');
  }

  // Mana conditions: mana_remaining > 40%, mana_pct <= 25, mana < 20%
  let m = raw.match(/^(?:mana_remaining|mana_pct|mana|mana_percent)\s*(<=|<|>=|>|==)\s*(\d+(?:\.\d+)?)\s*%?$/i);
  if (m) {
    const op = m[1] === '==' ? '<=' : m[1];
    return `mana_pct ${op} ${m[2]}`;
  }

  if (/^(?:dots_active|dots_missing)\(([1-9]|[12][0-9]|3[01])\)$/.test(raw)) return raw;
  if (['debuff.assigned_curse', '!debuff.assigned_curse'].includes(raw)) return raw;

  // Pet mana conditions: pet_mana < 115, pet_mana <= 160
  m = raw.match(/^(?:pet_mana|petmana)\s*(<=|<|>=|>|==)\s*(\d+(?:\.\d+)?)$/i);
  if (m) {
    const op = m[1] === '==' ? '<=' : m[1];
    return `pet_mana ${op} ${m[2]}`;
  }

  // Target HP conditions: target_hp < 35%, target_hp_pct <= 35, hp < 35%
  m = raw.match(/^(?:target_hp|target_hp_pct|target_health|target_health_pct|hp)\s*(<=|<|>=|>|==)\s*(\d+(?:\.\d+)?)\s*%?$/i);
  if (m) {
    const op = m[1] === '==' ? '<=' : m[1];
    return `target_hp_pct ${op} ${m[2]}`;
  }

  // Fight time / TTD conditions: target_ttd >= 60s, ttd >= 60, target_ttdie >= 60
  m = raw.match(/^(?:target_ttd|target_ttdie|ttd|ttdie)\s*(<=|<|>=|>|==)\s*(\d+(?:\.\d+)?)\s*s?$/i);
  if (m) {
    const op = m[1] === '==' ? '>=' : m[1];
    return `target_ttd ${op} ${m[2]}`;
  }

  // Buff / Proc conditions
  if (/^(?:buff\.)?shadow_trance(?:\s*active)?$/i.test(raw) || /^shadow\s+trance\s+active$/i.test(raw)) {
    return 'buff.shadow_trance';
  }
  if (/^(?:buff\.)?isb(?:\s*active)?$/i.test(raw)) {
    return 'buff.isb';
  }
  if (/^!?(?:buff\.|debuff\.)?isb_missing$/i.test(raw) || /^!(?:buff\.|debuff\.)?isb$/i.test(raw) || /^isb\s+missing$/i.test(raw)) {
    return 'buff.isb_missing';
  }

  // Decimation conditions
  if (/^decimation\.inactive$/i.test(raw) || /^decimation\s+inactive$/i.test(raw)) {
    return 'decimation.inactive';
  }
  if (/^decimation\.active$/i.test(raw) || /^decimation\s+active$/i.test(raw)) {
    return 'decimation.active';
  }

  // Demonic Brand missing
  if (/^(?:debuff\.)?demonic_brand_missing$/i.test(raw) || /^brand\s+missing$/i.test(raw) || /^demonic\s+brand\s+missing$/i.test(raw)) {
    return 'debuff.demonic_brand_missing';
  }

  // DoT remains / expired
  m = raw.match(/^(?:target\.)?debuff_remains\(\s*["']([^"']+)["']\s*\)\s*(<=|<|>=|>|==)\s*(\d+(?:\.\d+)?)\s*s?$/i);
  if (m) {
    const op = m[2] === '==' ? '<=' : m[2];
    return `target.debuff_remains("${m[1]}") ${op} ${m[3]}`;
  }

  m = raw.match(/^(?:dot_remains|dot_remaining|debuff_remains)\(\s*["']([^"']+)["']\s*\)\s*(<=|<|>=|>|==)\s*(\d+(?:\.\d+)?)\s*s?$/i);
  if (m) {
    const op = m[2] === '==' ? '<=' : m[2];
    return `target.debuff_remains("${m[1]}") ${op} ${m[3]}`;
  }

  m = raw.match(/^(?:dot_remains|dot_remaining|dot_rem|remains|remaining)\s*(<=|<|>=|>|==)\s*(\d+(?:\.\d+)?)\s*s?$/i);
  if (m) {
    const op = m[1] === '==' ? '<=' : m[1];
    const spellName = actionId === 'immo' ? 'Immolate' : actionId === 'corr' ? 'Corruption' : actionId === 'agony' ? 'Bane of Agony' : actionId === 'siphon' ? 'Siphon Life' : 'Corruption';
    return `target.debuff_remains("${spellName}") ${op} ${m[2]}`;
  }

  if (/^dot\s+expired$/i.test(raw) || /^missing\s+dot$/i.test(raw) || /^dot\s+down$/i.test(raw)) {
    const spellName = actionId === 'immo' ? 'Immolate' : actionId === 'corr' ? 'Corruption' : actionId === 'agony' ? 'Bane of Agony' : actionId === 'siphon' ? 'Siphon Life' : 'Corruption';
    return `target.debuff_remains("${spellName}") <= 0`;
  }

  if (/^immolate\s*remaining\s*(<=|<)\s*(\d+(?:\.\d+)?)\s*s?$/i.test(raw) || /^immolate_remains\s*(<=|<)\s*(\d+(?:\.\d+)?)\s*s?$/i.test(raw)) {
    const matchRem = raw.match(/\d+(?:\.\d+)?/);
    return `target.debuff_remains("Immolate") < ${matchRem ? matchRem[0] : '6'}`;
  }

  return raw;
}

export function parseAPLLine(line, lineNum = 1) {
  const trimmed = line.trim();
  if (!trimmed || trimmed.startsWith('#') || trimmed.startsWith('//')) {
    return null;
  }

  let actionStr = '';
  let condStr = 'true';

  const ifMatch = trimmed.match(/^([^#]+?)\s+(?:if|when)\s+(.+)$/i);
  if (ifMatch) {
    actionStr = ifMatch[1].trim();
    condStr = ifMatch[2].trim();
  } else {
    actionStr = trimmed.replace(/\s+#.*$/, '').trim();
  }

  const normActionKey = actionStr.toLowerCase().replace(/[\s\-_:]+/g, ' ');
  const cleanKey = actionStr.toLowerCase().replace(/[\s\-_:]+/g, '');
  const mappedId = ACTION_ALIASES[normActionKey] || ACTION_ALIASES[cleanKey] || ACTION_ALIASES[actionStr.toLowerCase()];

  if (!mappedId || !ACTION_INFO[mappedId]) {
    throw new Error(`Line ${lineNum}: Unknown action '${actionStr}'. Supported actions: sacrifice_succubus_summon_imp, berserking, blood_fury, eureka, bolt, tap, nightfall, corr, immo, conflag, shadowburn, incinerate, searing, drain, siphon, wrack, hellfire, curse, agony, brand, decimateSearing, decimateSoulFire`);
  }

  const info = ACTION_INFO[mappedId];
  const normalizedCond = normalizeConditionForAction(condStr, mappedId);

  const entry = {
    id: mappedId,
    spell: info.spell,
    icon: info.icon,
    condition: condStr === 'true' ? 'Always' : condStr,
    rawCond: normalizedCond,
    enabled: normalizedCond !== 'false',
    action: info.action,
  };

  // Validate compilation immediately to catch any malformed condition
  try {
    compileAPLToBytecode([entry]);
  } catch (err) {
    throw new Error(`Line ${lineNum} (${info.spell}): ${err.message}`);
  }

  return entry;
}

export function parseAPLText(text) {
  if (typeof text !== 'string') return [];
  const lines = text.split('\n');
  const rules = [];

  for (let i = 0; i < lines.length; i++) {
    const entry = parseAPLLine(lines[i], i + 1);
    if (entry) {
      rules.push(entry);
    }
  }

  if (rules.length === 0) {
    throw new Error('APL must contain at least one valid action rule.');
  }

  return rules;
}

export function formatAPLEntry(entry) {
  const actionName = CANONICAL_ACTION_NAMES[entry.id] || entry.id;
  const raw = String(entry.rawCond || 'true').trim();
  if (!raw || raw === 'true' || raw === 'always' || entry.condition === 'Always') {
    return actionName;
  }

  let formatted = raw
    .replace(/^mana_pct\s*(<=|<|>=|>)\s*(\d+(?:\.\d+)?)$/, (_, op, val) => `mana_remaining ${op} ${val}%`)
    .replace(/^target_hp_pct\s*(<=|<|>=|>)\s*(\d+(?:\.\d+)?)$/, (_, op, val) => `target_hp ${op} ${val}%`)
    .replace(/^target_ttd\s*(>=|>)\s*(\d+(?:\.\d+)?)$/, (_, op, val) => `target_ttd ${op} ${val}s`)
    .replace(/^target\.debuff_remains\(\s*["']([^"']+)["']\s*\)\s*(<=|<)\s*0(?:\.0+)?$/, 'dot_remains <= 0')
    .replace(/^target\.debuff_remains\(\s*["']([^"']+)["']\s*\)\s*(<=|<)\s*(\d+(?:\.\d+)?)$/, (_, spell, op, val) => `dot_remains("${spell}") ${op} ${val}s`);

  return `${actionName} if ${formatted}`;
}

export function formatAPLToText(aplList = []) {
  return (aplList || [])
    .filter(rule => isAPLRuleEnabled(rule))
    .map(formatAPLEntry)
    .join('\n');
}

export function compileAPLToBytecode(aplList = []) {
  const mapAction = {
    elements: APL_ACTION.CURSE_OF_ELEMENTS, spellblasting: APL_ACTION.SPELLBLASTING,
    amplify: APL_ACTION.AMPLIFY_CURSE,
    petSwap: APL_ACTION.SACRIFICE_SUCCUBUS_SUMMON_IMP,
    berserking: APL_ACTION.BERSERKING,
    bloodFury: APL_ACTION.BLOOD_FURY,
    eureka: APL_ACTION.EUREKA,
    tap: APL_ACTION.LIFE_TAP,
    nightfall: APL_ACTION.NIGHTFALL_SHADOW_BOLT,
    brand: APL_ACTION.DEMONIC_BRAND_SEARING_PAIN,
    decimateSearing: APL_ACTION.DECIMATION_SEARING_PAIN,
    decimateSoulFire: APL_ACTION.DECIMATION_SOUL_FIRE,
    curse: APL_ACTION.CURSE_OF_DOOM,
    agony: APL_ACTION.CURSE_OF_AGONY,
    corr: APL_ACTION.CORRUPTION,
    immo: APL_ACTION.IMMOLATE,
    conflag: APL_ACTION.CONFLAGRATE,
    shadowburn: APL_ACTION.SHADOWBURN,
    incinerate: APL_ACTION.INCINERATE_FILLER,
    searing: APL_ACTION.SEARING_PAIN_FILLER,
    drain: APL_ACTION.DRAIN_SOUL_FILLER,
    siphon: APL_ACTION.SIPHON_LIFE,
    wrack: APL_ACTION.DRAIN_HOPE,
    hellfire: APL_ACTION.HELLFIRE,
    bolt: APL_ACTION.SHADOW_BOLT_FILLER,
  };

  const spellIds = {
    'corruption': 1, 'curse of agony': 2, 'bane of agony': 2, 'curse of doom': 22, 'bane of doom': 22,
    'immolate': 3, 'siphon life': 15,
  };
  const spellFromText = text => {
    const name = String(text).toLowerCase();
    const found = Object.keys(spellIds).find(spell => name.includes(spell));
    return found ? spellIds[found] : 0;
  };
  const parseCondition = (entry) => {
    const raw = String(entry.rawCond || 'true').trim().toLowerCase();
    const match = (pattern) => raw.match(pattern);
    if (raw === 'buff.shadow_trance')
      return { cond: APL_COND.SHADOW_TRANCE, param: 0, targetSpell: 0 };
    if (raw === 'debuff.demonic_brand_missing')
      return { cond: APL_COND.DEMONIC_BRAND_MISSING, param: 0, targetSpell: 0 };
    if (entry.id === 'decimateSearing' && raw === 'decimation.inactive')
      return { cond: APL_COND.DECIMATION_INACTIVE, param: 35, targetSpell: 5 };
    if (entry.id === 'decimateSoulFire' && raw === 'decimation.active')
      return { cond: APL_COND.DECIMATION_ACTIVE, param: 35, targetSpell: 13 };
    if (raw === 'buff.decimation') return { cond: APL_COND.DECIMATION_AURA_ACTIVE, param: 0, targetSpell: 0 };
    if (raw === '!buff.decimation') return { cond: APL_COND.DECIMATION_AURA_INACTIVE, param: 0, targetSpell: 0 };
    const dots = raw.match(/^(dots_active|dots_missing)\((\d+)\)$/);
    if (dots) return { cond: dots[1] === 'dots_active' ? APL_COND.DOTS_ACTIVE : APL_COND.DOTS_MISSING, param: Number(dots[2]), targetSpell: 0 };
    if (raw === 'debuff.assigned_curse' || raw === '!debuff.assigned_curse') return { cond: raw[0] === '!' ? APL_COND.ASSIGNED_CURSE_INACTIVE : APL_COND.ASSIGNED_CURSE_ACTIVE, param: 0, targetSpell: 0 };
    const executeTime = raw.match(/^time_to_execute\((\d+)\)\s*(<=|<|>=|>)\s*(-?\d+(?:\.\d+)?)$/);
    if (executeTime) {
      const percent = Number(executeTime[1]);
      if (percent > 100) throw new Error('Execute percentage must be between 0 and 100');
      return { cond: ({ '<=': APL_COND.TIME_TO_EXECUTE_LE, '>=': APL_COND.TIME_TO_EXECUTE_GE,
        '<': APL_COND.TIME_TO_EXECUTE_LT, '>': APL_COND.TIME_TO_EXECUTE_GT })[executeTime[2]],
        param: Number(executeTime[3]), targetSpell: percent };
    }
    if (raw === 'buff.amplify_curse') return { cond: APL_COND.AMPLIFY_ACTIVE, param: 0, targetSpell: 0 };
    if (raw === '!buff.amplify_curse') return { cond: APL_COND.AMPLIFY_INACTIVE, param: 0, targetSpell: 0 };
    if (raw === 'buff.isb') return { cond: APL_COND.ISB_ACTIVE, param: 0, targetSpell: 0 };
    if (raw === 'buff.isb_missing' || raw === '!buff.isb' || raw === 'debuff.isb_missing')
      return { cond: APL_COND.ISB_INACTIVE, param: 0, targetSpell: 0 };
    if (!raw || raw === 'true') return { cond: APL_COND.ALWAYS, param: 0, targetSpell: 0 };

    let m = match(/^mana_pct\s*(<=|<|>=|>)\s*(\d+(?:\.\d+)?)$/);
    if (m) return { cond: ({'<': APL_COND.MANA_LT, '<=': APL_COND.MANA_LE,
      '>': APL_COND.MANA_GT, '>=': APL_COND.MANA_GE})[m[1]],
      param: Number(m[2]), targetSpell: 0 };
    if (raw.match(/^mana_pct\s*<=\s*(\d+)$/))
      return { cond: APL_COND.MANA_LE, param: Number(raw.match(/^mana_pct\s*<=\s*(\d+)$/)[1]), targetSpell: 0 };
    m = match(/^pet_mana\s*(<=|<|>=|>)\s*(\d+(?:\.\d+)?)$/);
    if (m) return { cond: ({'<': APL_COND.PET_MANA_LT, '<=': APL_COND.PET_MANA_LE,
      '>': APL_COND.PET_MANA_GT, '>=': APL_COND.PET_MANA_GE})[m[1]],
      param: Number(m[2]), targetSpell: 0 };
    m = match(/^target_hp_pct\s*(<=|<|>=|>)\s*(\d+(?:\.\d+)?)$/);
    if (m) return { cond: ({'<': APL_COND.TARGET_HP_LT, '<=': APL_COND.TARGET_HP_LE,
      '>': APL_COND.TARGET_HP_GT, '>=': APL_COND.TARGET_HP_GE})[m[1]],
      param: Number(m[2]), targetSpell: 0 };
    m = match(/^target_ttd\s*(>=|>)\s*(\d+(?:\.\d+)?)$/);
    if (m) return { cond: m[1] === '>' ? APL_COND.FIGHT_TIME_GT : APL_COND.FIGHT_TIME_GE, param: Number(m[2]), targetSpell: 0 };
    m = match(/^target_ttd\s*(<=|<)\s*(\d+(?:\.\d+)?)$/);
    if (m) return { cond: m[1] === '<' ? APL_COND.FIGHT_TIME_LT : APL_COND.FIGHT_TIME_LE, param: Number(m[2]), targetSpell: 0 };

    m = match(/^target\.debuff_remains\(\s*["']([^"']+)["']\s*\)\s*(<=|<|>=|>)\s*(\d+(?:\.\d+)?)$/);
    if (m) {
      const targetSpell = spellFromText(m[1]);
      if (!targetSpell) throw new Error(`APL condition refers to unsupported debuff: ${m[1]}`);
      const cond = m[2] === '<' ? APL_COND.DOT_REM_LT : (m[2] === '<=' ? APL_COND.DOT_REM_LE : (m[2] === '>' ? APL_COND.DOT_REM_GT : APL_COND.DOT_REM_GE));
      return { cond, param: Number(m[3]), targetSpell };
    }

    const hasNightfall = /talent\.nightfall/.test(raw);
    const ttd = raw.match(/target_ttd\s*>=?\s*(\d+(?:\.\d+)?)/);
    const missing = [...raw.matchAll(/!target\.has_debuff\(\s*["']([^"']+)["']\s*\)/g)]
      .map(x => spellFromText(x[1]));
    const onlySupportedAtoms = raw.split(/\s*&&\s*/).every(atom =>
      /^target_ttd\s*>=?\s*\d+(?:\.\d+)?$/.test(atom.trim()) ||
      /^!target\.has_debuff\(\s*["'][^"']+["']\s*\)$/.test(atom.trim()) ||
      /^talent\.nightfall$/.test(atom.trim()));
    if (onlySupportedAtoms && missing.length && missing.every(Boolean)) {
      if (hasNightfall && !ttd && missing.length === 1) {
        return { cond: APL_COND.NIGHTFALL_DOT_MISSING, param: 0, targetSpell: missing[0] };
      }
      if (ttd && missing.length === 1) {
        return { cond: APL_COND.FIGHT_GE_DOT_MISSING, param: Number(ttd[1]), targetSpell: missing[0] };
      }
      if (ttd && missing.length === 2 && (missing.includes(2) || missing.includes(22))) {
        return { cond: APL_COND.FIGHT_GE_DOOM_AGONY_MISSING, param: Number(ttd[1]), targetSpell: 2 };
      }
    }
    if (/^!target\.has_debuff\(\s*["'](?:curse of doom|bane of doom)["']\s*\)$/i.test(raw))
      return { cond: APL_COND.DOOM_MISSING, param: 0, targetSpell: 2 };
    m = match(/^!target\.has_debuff\(\s*["']([^"']+)["']\s*\)$/);
    if (m && spellFromText(m[1]))
      return { cond: APL_COND.FIGHT_GE_DOT_MISSING, param: 0, targetSpell: spellFromText(m[1]) };
    throw new Error(`APL condition is not supported by the shader: ${entry.rawCond}`);
  };

  return (aplList || currentAPL).map(entry => {
    if (entry.enabled && entry.id === 'drain')
      throw new Error('Drain Soul is not implemented by the WebGL shader yet.');
    const action = (entry.action !== undefined && entry.action !== null) ? entry.action : mapAction[entry.id];
    if (action === undefined) throw new Error(`Unsupported APL action: ${entry.id}`);
    
    const enabled = isAPLRuleEnabled(entry);
    let first = { cond: APL_COND.ALWAYS, param: 0, targetSpell: 0 };
    let second = { cond: APL_COND.ALWAYS, param: 0, targetSpell: 0 };
    if (entry.cond1 != null || entry.cond != null) {
      first = { cond: entry.cond1 ?? entry.cond, param: Number(entry.param1 ?? entry.param ?? 0),
        targetSpell: Number(entry.targetSpell1 ?? entry.targetSpell ?? 0) };
      second = { cond: entry.cond2 ?? APL_COND.ALWAYS, param: Number(entry.param2 ?? 0),
        targetSpell: Number(entry.targetSpell2 ?? 0) };
    } else if (enabled) {
      // Amplify's readiness guard already enforces aura inactivity. Remove only
      // that redundant conjunct so two remaining source predicates fit bytecode.
      if (action === APL_ACTION.AMPLIFY_CURSE) {
        const parts = String(entry.rawCond || 'true').split(/\s*&&\s*/);
        entry = { ...entry, rawCond: parts.filter(part => part.trim() !== '!buff.amplify_curse').join(' && ') || 'true' };
      }
      try {
        first = parseCondition(entry);
      } catch (error) {
        // Keep existing compound conditions, then try two shader conditions joined by &&.
        const parts = String(entry.rawCond || '').split(/\s*&&\s*/);
        if (parts.length !== 2) throw error;
        first = parseCondition({ ...entry, rawCond: parts[0] });
        second = parseCondition({ ...entry, rawCond: parts[1] });
      }
    }

    return {
      action, ...first,
      cond1: first.cond, param1: first.param, targetSpell1: first.targetSpell,
      cond2: second.cond, param2: second.param, targetSpell2: second.targetSpell,
      enabled: enabled ? 1 : 0
    };
  });
}

