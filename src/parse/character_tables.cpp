#include "parse/character_tables.h"

namespace CharacterTables {
const char *const races[] = {
    "Monster",
    "Dwarf",
    "Elf",
    "Gnome",
    "Half-Elf",
    "Halfling",
    "Half-Orc",
    "Human",
};
const int racesCount = 8;

const char *const classes[] = {
    "Cleric",
    "Druid",
    "Fighter",
    "Paladin",
    "Ranger",
    "Magic-User",
    "Thief",
    "Monk",
    "Cleric/Fighter",
    "Cleric/Fighter/Magic-User",
    "Cleric/Ranger",
    "Cleric/Magic-User",
    "Cleric/Thief",
    "Fighter/Magic-User",
    "Fighter/Thief",
    "Fighter/Magic-User/Thief",
    "Magic-User/Thief",
};
const int classesCount = 17;

const char *const alignments[] = {
    "Lawful Good",
    "Lawful Neutral",
    "Lawful Evil",
    "Neutral Good",
    "True Neutral",
    "Neutral Evil",
    "Chaotic Good",
    "Chaotic Neutral",
    "Chaotic Evil",
};
const int alignmentsCount = 9;

const char *const spells[] = {
    "Bless",
    "Curse",
    "Cure Light Wounds",
    "Cause Light Wounds",
    "Detect Magic",
    "Protection From Evil",
    "Protection From Good",
    "Resist Cold",
    "Burning Hands",
    "Charm Person",
    "Detect Magic",
    "Enlarge",
    "Reduce",
    "Friends",
    "Magic Missile",
    "Protection From Evil",
    "Protection From Good",
    "Read Magic",
    "Shield",
    "Shocking Grasp",
    "Sleep",
    "Find Traps",
    "Hold Person",
    "Resist Fire",
    "Silence 15' Radius",
    "Slow Poison",
    "Snake Charm",
    "Spritual Hammer",
    "Detect Invisibility",
    "Invisibility",
    "Knock",
    "Mirror Image",
    "Ray of Enfeeblement",
    "Stinking Cloud",
    "Strength",
    "Animate Dead",
    "Cure Blindness",
    "Cause Blindness",
    "Cure Disease",
    "Cause Disease",
    "Dispel Magic",
    "Prayer",
    "Remove Curse",
    "Bestow Curse",
    "Blink",
    "Dispel Magic",
    "Fireball",
    "Haste",
    "Hold Person",
    "Invisbility 10' Radius",
    "Lightning Bolt",
    "Protection From Evil 10' Radius",
    "Protection From Good 10' Radius",
    "Protection From Normal Missiles",
    "Slow",
    "Restoration",
};
const int spellsCount = 56;

const char *const itemTable[] = {
    "",
    "Battle Axe",
    "Hand Axe",
    "Bardiche",
    "Bec De Corbin",
    "Bill-Guisarme",
    "Bo Stick",
    "Club",
    "Dagger",
    "Dart",
    "Fauchard",
    "Fauchard-Fork",
    "Flail",
    "Military Fork",
    "Glaive",
    "Glaive-Guisarme",
    "Guisarme",
    "Guisarme-Voulge",
    "Halberd",
    "Lucern Hammer",
    "Hammer",
    "Javelin",
    "Jo Stick",
    "Mace",
    "Morning Star",
    "Partisan",
    "Military Pick",
    "Awl Pike",
    "Quarrel",
    "Ranseur",
    "Scimitar",
    "Spear",
    "Spetum",
    "Quarter Staff",
    "Bastard Sword",
    "Broad Sword",
    "Long Sword",
    "Short Sword",
    "Two-Handed Sword",
    "Trident",
    "Voulge",
    "Composite Long Bow",
    "Composite Short Bow",
    "Long Bow",
    "Short Bow",
    "Heavy Crossbow",
    "Light Crossbow",
    "Sling",
    "Mail",
    "Armor",
    "Leather",
    "Padded",
    "Studded",
    "Ring",
    "Scale",
    "Chain",
    "Splint",
    "Banded",
    "Plate",
    "Shield",
    "Woods",
    "Arrow",
    "",
    "",
    "Potion",
    "Scroll",
    "Ring",
    "Rod",
    "Stave",
    "Wand",
    "Jug",
    "Amulet",
    "Apparatus",
    "Bag",
    "Beaker",
    "Boat",
    "Book",
    "Boots",
    "Bowl",
    "Bracers",
    "Brazier",
    "Brooch",
    "Broom",
    "Purse",
    "Candle",
    "Carpet",
    "Censer",
    "Chime",
    "Cloak",
    "Crystal",
    "Cube",
    "Cubic",
    "Fortress",
    "Decanter",
    "Deck",
    "Drums",
    "Dust",
    "Eyes",
    "Figurine",
    "Flask",
    "Gauntlets",
    "Gem",
    "Girdle",
    "Helm",
    "Horn",
    "Horseshoes",
    "Incense",
    "Stone",
    "Instrument",
    "Javelin",
    "Jewel",
    "Ointment",
    "Libram",
    "Lyre",
    "Manual",
    "Mattock",
    "Maul",
    "Medallion",
    "Mirror",
    "Necklace",
    "Net",
    "Pigment",
    "Pearl",
    "Periapt",
    "Phylactery",
    "Pipes",
    "Hole",
    "Token",
    "Robe",
    "Rope",
    "Rug",
    "Saw",
    "Scarab",
    "Spade",
    "Sphere",
    "",
    "Talisman",
    "Tome",
    "Trident",
    "Grimoire",
    "Well",
    "Wings",
    "Vial",
    "Lantern",
    "",
    "Oil",
    "10 ft. Pole",
    "50 ft. Rope",
    "Iron",
    "Thf Prickly Tools",
    "Iron Rations",
    "Standard Rations",
    "Holy Symbol",
    "Holy Water vial",
    "Unholy Water vial",
    "Barding",
    "Dragon",
    "Lightning",
    "Saddle",
    "Small Raft",
    "Cart",
    "Wagon",
    "+1",
    "+2",
    "+3",
    "+4",
    "+5",
    "of",
    "",
    "Cloak",
    "Displacement",
    "Torches",
    "Oil",
    "Speed",
    "Tapestry",
    "Bodily Health",
    "Copper",
    "Silver",
    "Electrum",
    "Gold",
    "Platinum",
    "Ointment",
    "Keoghtum's",
    "Sheet",
    "Strength",
    "Healing",
    "Holding",
    "Extra",
    "Gaseous Form",
    "Slipperiness",
    "Jewelled",
    "Flying",
    "Treasure Finding",
    "Fear",
    "Disappearance",
    "Statuette",
    "Fungus",
    "Chain",
    "Pendant",
    "Broach",
    "Of Seeking",
    "-1",
    "-2",
    "-3",
    "Lightning Bolt",
    "Fire Resistance",
    "Magic Missiles",
    "Save",
    "Clerical Scroll",
    "Magic User Scroll",
    "With 1 Spell",
    "With 2 Spells",
    "With 3 Spells",
    "Protection Scroll",
    "Jewelry",
    "Fine",
    "Huge",
    "Bone",
    "Brass",
    "Key",
    "AC 2",
    "AC 6",
    "AC 4",
    "AC 3",
    "Of Protection",
    "Paralyzation",
    "Ogre Power",
    "Invisibility",
    "Missiles",
    "Elvenkind",
    "Rotting",
    "Covered",
    "Efreeti",
    "Bottle",
    "Missile Attractor",
    "Of Maglubiyet",
    "Secr Door & Trap Det",
    "Gd Dragon Control",
    "Feather Falling",
    "Giant Strength",
    "Restoring Level(s)",
    "Flame Tongue",
    "Fireballs",
    "Spiritual",
    "Boulder",
    "Diamond",
    "Emerald",
    "Opal",
    "Saphire",
    "Of Tyr",
    "Of Tempus",
    "Of Sune",
    "Wooden",
    "+3 vs Undead",
    "Pass",
    "Cursed",
};
const int itemTableCount = 256;

const char *const itemTypes[] = {
    "",
    "Battle Axe",
    "Hand Axe",
    "Bardiche",
    "Bec De Corbin",
    "Bill-Guisarme",
    "Bo Stick",
    "Club",
    "Dagger",
    "Dart",
    "Fauchard",
    "Fauchard-Fork",
    "Flail",
    "Military Fork",
    "Glaive",
    "Glaive-Guisarme",
    "Guisarme",
    "Guisarme-Voulge",
    "Halberd",
    "Lucern Hammer",
    "Hammer",
    "Javelin",
    "Jo Stick",
    "Mace",
    "Morning Star",
    "Partisan",
    "Military Pick",
    "Awl Pike",
    "Quarrel",
    "Ranseur",
    "Scimitar",
    "Spear",
    "Spetum",
    "Quarter Staff",
    "Bastard Sword",
    "Broad Sword",
    "Long Sword",
    "Short Sword",
    "Two-Handed Sword",
    "Trident",
    "Voulge",
    "Composite Long Bow",
    "Composite Short Bow",
    "Long Bow",
    "Short Bow",
    "Heavy Crossbow",
    "Light Crossbow",
    "Sling",
    "Mail",
    "Armor",
    "Leather Armor",
    "Padded Armor",
    "Studded Armor",
    "Ring Armor",
    "Scale Armor",
    "Chain Armor",
    "Splint Armor",
    "Banded Armor",
    "Plate Armor",
    "Shield",
    "Woods",
    "Magic User Scroll",
    "Cleric Scroll",
    "",
    "Miscelleanous",
    "Scroll",
    "Ring",
    "Rod",
    "Stave",
    "Miscelleanous Item",
    "Miscelleanous Item",
    "Miscelleanous Item",
    "Miscelleanous Item",
    "Arrows",
    "Beaker",
    "Key",
    "Book",
    "Clothing",
    "Wand",
    "Wand",
    "Brazier",
    "Brooch",
    "Broom",
    "Purse",
    "Candle",
    "Vial",
    "Censer",
    "Chime",
    "Cloak",
    "Crystal",
    "Ring",
    "Pass",
    "Cloak",
    "Ring",
    "Deck",
    "Drums",
    "Dust",
    "Eyes",
    "Figurine",
    "Flask",
    "Gauntlets",
    "Gem",
    "Girdle",
    "Helm",
    "Horn",
    "Horseshoes",
    "Incense",
    "Stone",
    "Instrument",
    "Javelin",
    "Jewel",
    "Ointment",
    "Libram",
    "Lyre",
    "Manual",
    "Mattock",
    "Maul",
    "Medallion",
    "Mirror",
    "Necklace",
    "Net",
    "Pigment",
    "Pearl",
    "Periapt",
    "Phylactery",
    "Pipes",
    "Hole",
    "Token",
};
const int itemTypesCount = 128;

const char *specialAbility(int value)
{
    static const char *const table[] = {
        nullptr, // 0x00
        "bless", // 0x01
        "cursed", // 0x02
        "healing", // 0x03
        "dispel_evil", // 0x04
        "detect_magic", // 0x05
        "affect_06", // 0x06
        "faerie_fire", // 0x07
        "protection_from_evil", // 0x08
        "protection_from_good", // 0x09
        "resist_cold", // 0x0A
        "charm_person", // 0x0B
        "enlarge", // 0x0C
        "reduce", // 0x0D
        "friends", // 0x0E
        "poison_damage", // 0x0F
        "read_magic", // 0x10
        "shield", // 0x11
        "gnome_vs_man_sized_giant", // 0x12
        "find_traps", // 0x13
        "resist_fire", // 0x14
        "silence_15_radius", // 0x15
        "slow_poison", // 0x16
        "spiritual_hammer", // 0x17
        "detect_invisibility", // 0x18
        "invisibility", // 0x19
        "dwarf_vs_orc", // 0x1A
        "fumbling", // 0x1B
        "mirror_image", // 0x1C
        "ray_of_enfeeblement", // 0x1D
        "stinking_cloud", // 0x1E
        "helpless", // 0x1F
        "animate_dead", // 0x20
        "blinded", // 0x21
        "cause_disease_1", // 0x22
        "confuse", // 0x23
        "bestow_curse", // 0x24
        "blink", // 0x25
        "strength", // 0x26
        "haste", // 0x27
        "affect_28", // 0x28
        "prot_from_normal_missiles", // 0x29
        "slow", // 0x2A
        "weaken", // 0x2B
        "cause_disease_2", // 0x2C
        "prot_from_evil_10_radius", // 0x2D
        "prot_from_good_10_radius", // 0x2E
        "dwarf_and_gnome_vs_giants", // 0x2F
        "affect_30", // 0x30
        "prayer", // 0x31
        "hot_fire_shield", // 0x32
        "lightning", // 0x33
        "paralyze", // 0x34
        "sleep", // 0x35
        "cold_fire_shield", // 0x36
        "poisoned", // 0x37
        "item_invisibility", // 0x38
        "affect_39", // 0x39
        "clear_movement", // 0x3A
        "regenerate", // 0x3B
        "resist_normal_weapons", // 0x3C
        "fire_resist", // 0x3D
        "highConRegen", // 0x3E
        "minor_globe_of_invulnerability", // 0x3F
        "poison_plus_0", // 0x40
        "poison_plus_4", // 0x41
        "poison_plus_2", // 0x42
        "thri_kreen_paralyze", // 0x43
        "ghoul_paralyze", // 0x44
        "drider_paralyzation_poison", // 0x45
        "poison_neg_2", // 0x46
        "invisible", // 0x47
        "unknown_thief_power_backstab?", // 0x48
        "tiger_claw_rake", // 0x49
        "affect_4a", // 0x4A
        "weap_dragon_slayer", // 0x4B
        "stirge_blood_suck", // 0x4C
        "giant_mantis_bite_and_hold", // 0x4D
        "affect_4e", // 0x4E
        "tyranthraxus_unknown_power1", // 0x4F
        "ankheg_acid_attack", // 0x50
        "tyranthraxus_unknown_power_half_damage?", // 0x51
        "mummy_only_power_cold_immunity?", // 0x52
        "lightning", // 0x53
        "unknown_vampire_only_power_charm_gaze?", // 0x54
        "unknown_undead_power0", // 0x55
        "undead_power_double_level_drain?", // 0x56
        "mummy_only_power_half_damage_from_weapons?", // 0x57
        "magic missile attack", // 0x58
        "displace", // 0x59
        "dwarf/halfling resist magic/poison?", // 0x5A
        "juju_zombie_power_half_damage_from_piercing_and_blunt_weapons?", // 0x5B
        "affect_5c", // 0x5C
        "unknown_juju_zombie_power1", // 0x5D
        "unknown_juju_zombie_power2", // 0x5E
        "affect_5F", // 0x5F
        "owlbear_hug_check", // 0x60
        "con_saving_bonus", // 0x61
        "regen_3_hp", // 0x62
        "wild_boar_fight_until_-10hp", // 0x63
        "troll_fire_or_acid", // 0x64
        "troll_regen", // 0x65
        "TrollRegen", // 0x66
        "unknown_vampire_power", // 0x67
        "thri_kreen_dodge_missile", // 0x68
        "resist_magic_50_percent", // 0x69
        "resist_magic_15_percent", // 0x6A
        "elf_resist_sleep", // 0x6B
        "protect_charm_sleep", // 0x6C
        "resist_paralyze", // 0x6D
        "unknown_undead_power1", // 0x6E
        "unknown_undead_power2", // 0x6F
        "immune_to_fire", // 0x70
        "efreet_fire_resistance", // 0x71
        "unknown_vampire_resistance?", // 0x72
        "unknown_skeleton_resistance_half_damage_from_edged_weapons?", // 0x73
        "unknown_mummy_power_fire_vulnerability?", // 0x74
        "unknown_undead_power_3?", // 0x75
        "unknown_vampire_only_power", // 0x76
        "unknown_undead_power4", // 0x77
        "giant_rock_hurling", // 0x78
        "ankheg_acid_attack?", // 0x79
        "dracolich_paralysis", // 0x7A
        "affect_7b", // 0x7B
        "halfelf_resistance", // 0x7C
        "unknown_undead_power5", // 0x7D
        "unknown_vampire_power", // 0x7E
        "gaze_attack_turn_to_stone", // 0x7F
        "affect_80", // 0x80
        "protect_magic", // 0x81
        "affect_82", // 0x82
        "cast_breath_fire", // 0x83
        "cast_throw_lightening", // 0x84
        "affect_85", // 0x85
        "ranger_vs_giant", // 0x86
        "protect_elec", // 0x87
        "entangle", // 0x88
        "affect_89", // 0x89
        "affect_8a", // 0x8A
        "affect_8b", // 0x8B
        "paladinDailyHealCast", // 0x8C
        "paladinDailyCureRefresh", // 0x8D
        "fear", // 0x8E
        "affect_8f", // 0x8F
        "owlbear_hug_round_attack", // 0x90
        "sp_dispel_evil", // 0x91
        "strength_spell", // 0x92
        "do_items_affect", // 0x93
    };
    if (value < 0 || value >= 148) return "unknown effect";
    return table[value] ? table[value] : "unknown effect";
}

} // namespace CharacterTables
