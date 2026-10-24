-- Only actions implemented by the source kernel are offered. Drain Soul/Life
-- IDs currently execute Shadow Bolt there and are deliberately excluded here.
local vm=require('sim.classes.warlock.generated.apl')
local R={classId='warlock',schemaVersion=1,actions={},byLabel={}}
local function add(name,label,key,condition)
 local action={name=name,label=label,id=assert(vm.actions[key]),defaultCondition=condition or 'true'}
 R.actions[name]=action;R.byLabel[label]=action
end
add('life_tap','Life Tap','LIFE_TAP','mana_pct <= 25')
add('shadow_bolt','Shadow Bolt','SHADOW_BOLT_FILLER')
add('corruption','Corruption','CORRUPTION','dot.corruption.remains <= 0')
add('bane_of_agony','Bane of Agony','CURSE_OF_AGONY','dot.agony.remains <= 0')
add('bane_of_doom','Bane of Doom','CURSE_OF_DOOM','target_ttd >= 60')
add('immolate','Immolate','IMMOLATE','dot.immolate.remains <= 0')
add('incinerate','Incinerate','INCINERATE_FILLER')
add('searing_pain','Searing Pain','SEARING_PAIN_FILLER')
add('siphon_life','Siphon Life','SIPHON_LIFE','dot.siphon_life.remains <= 0')
add('wrack','Wrack','DRAIN_HOPE')
add('conflagrate','Conflagrate','CONFLAGRATE')
add('shadowburn','Shadowburn','SHADOWBURN')
add('nightfall','Nightfall: Shadow Bolt','NIGHTFALL_SHADOW_BOLT','buff.shadow_trance')
add('demonic_brand','Demonic Brand','DEMONIC_BRAND_SEARING_PAIN','debuff.demonic_brand_missing')
add('soul_fire','Soul Fire','DECIMATION_SOUL_FIRE')
add('hellfire','Hellfire','HELLFIRE')
add('blood_fury','Blood Fury','BLOOD_FURY')
add('berserking','Berserking','BERSERKING')
add('eureka','Eureka!','EUREKA')
add('amplify_curse','Amplify Curse','AMPLIFY_CURSE','!buff.amplify_curse')
add('curse_of_elements','Curse of the Elements','CURSE_OF_ELEMENTS','!debuff.assigned_curse')
add('spellblasting','Major Spellblasting Potion','SPELLBLASTING')
add('sacrifice_succubus_summon_imp','Sacrifice Succubus + Summon Imp','SACRIFICE_SUCCUBUS_SUMMON_IMP')
R.dots={corruption=1,agony=2,immolate=3,siphon_life=15,doom=22}
return R
