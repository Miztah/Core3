-- Table 0 is for mobiles spawned in all phases
-- { template, respawn, x, z, y, heading, npcFunction, convoTemplate }
-- npcFunction and convoTemplate are left as "" if unused

villageMobileSpawns = {
	[0] = {
		{ "paemos", 5289, 78, -4149, 240, "", "" },
		{ "noldan", 5243, 78, -4224, 0, "", "" },
		{ "fs_villager_farmer", 5291.31, 78.5, -4037.8, 164, "", "" },
		{ "eozlin", 5200.15, 78.5, -4141.37, 164, "", "villageSurveyorConvoTemplate" },
	},

	[1] = {
		{ "captain_sarguillo", 5313.9, 78.5, -4161.5, -72, "", "villageSarguilloPhase1ConvoTemplate" },
		{ "rohak_village_elder", 5306, 78, -4145, 0, "", "villageElderPhase1ConvoTemplate" },
		{ "quharek", 5373.7, 78.5, -4181.4, -69, "", "" },
		{ "whip", 5283.66, 78.5, -4226.31, -143, "", "villageWhipPhase1ConvoTemplate" },
		{ "sivarra_mechaux", 5391.5, 78.5, -4075.7, -110, "", "villageSivarraPhase1ConvoTemplate" },
		{ "fs_surgical_droid_21b", 5392.1, 78.5, -4072.5, 101, "initMedDroid", "villageMedicalDroidPhase1ConvoTemplate" },
		{ "fs_surgical_droid_21b", 5396.4, 78.5, -4074.7, -104, "initMedDroid", "villageMedicalDroidPhase1ConvoTemplate" },
		{ "fs_surgical_droid_21b", 5158.11, 78.5, -4116.02, -30, "initMedDroid", "villageMedicalDroidPhase1ConvoTemplate" },

		{ "fs_villager_generic", 5388.3, 78.5, -4074.9, 131, "initWoundedVillager", "" },
		{ "fs_villager_generic", 5394.1, 78.5, -4073.2, -130, "initWoundedVillager", "" },
		{ "fs_villager_generic", 5395.1, 78.5, -4077.4, -35, "initWoundedVillager", "" },
		{ "fs_villager_generic", 5392.9, 78.5, -4079.1, -43, "initWoundedVillager", "" },
		{ "fs_villager_generic", 5155.2, 78.5, -4116.3, 12, "initWoundedVillager", "" },
		{ "fs_villager_generic", 5157, 78.5, -4109.7, -167, "initWoundedVillager", "" },

		{ "fs_villager_male", 5346.7, 78.5, -4062.2, -46, "", "fsVillageConvo6MaleTemplate" },
		{ "fs_villager_female", 5227.3, 78.5, -4065.5, -101, "", "fsVillageConvo4FemaleTemplate" },
		{ "fs_villager_generic", 5159.4, 78.5, -4110.8, 147, "initPukingVillager", "" },
		{ "fs_villager_generic", 5391.8, 78.5, -4074.6, -106, "initPukingVillager", "" },
		{ "fs_villager_generic", 5388.5, 78.5, -4076.5, -119, "initPukingVillager", "" },
		{ "fs_villager_generic", 5176.9, 78.5, -4113.3, -138, "initPanickedVillager", "" },
		{ "fs_villager_generic", 5173.5, 78.5, -4117.5, 36, "initPanickedVillager", "" },
		{ "fs_villager_generic", 5218, 78.5, -4190.5, 88, "initPanickedVillager", "" },
		{ "fs_villager_generic", 5255.2, 78.5, -4225.3, -102, "initPanickedVillager", "" },
		{ "fs_villager_generic", 5322.3, 78.5, -4205.6, -95, "initPanickedVillager", "" },
		{ "fs_villager_generic", 5341.4, 78.5, -4210.2, -65, "initPanickedVillager", "" },
		{ "fs_villager_generic", 5397.9, 78.5, -4198.01, -82, "initPanickedVillager", "" },
		{ "fs_villager_generic", 5393.9, 78.5, -4132.5, -1, "initPanickedVillager", "" },
		{ "fs_villager_generic", 5320.2, 78.5, -4136.4, -131, "initPanickedVillager", "" },
		{ "fs_villager_generic", 5314.3, 78.5, -4142.5, 46, "initPanickedVillager", "" },
		{ "fs_villager_generic", 5197.7, 78.5, -4071.7, 122, "", "fsVillageConvo7Template" },
	},
	[2] = {
		{ "captain_sarguillo", 5313.9, 78.5, -4161.5, -72, "", "" },
		{ "dageerin", 5238.25, 78.5, -4189, 97, "", "" },
		{ "whip", 5283.66, 78.5, -4226.31, -143, "", "" },
		{ "quharek", 5373.7, 78.5, -4181.4, -69, "", "" },
		{ "fs_cll8_binary_load_lifter", 5193.25, 78.5, -4195.6, -34, "", "" },

		{ "fs_villager_male", 5346.7, 78.5, -4062.15, -46, "", "fsVillageConvo6MaleTemplate" },
		{ "fs_villager_female", 5199.3, 78.5, -4128.55, -142, "", "fsVillageConvo5FemaleTemplate" },
		{ "fs_villager_generic", 5259.1, 78.5, -4243.2, -52, "initVillageRepairer", "" },
		{ "fs_villager_generic", 5254.15, 78.5, -4243, 56, "initVillageRepairer", "" },
		{ "fs_villager_generic", 5287.4, 78.5, -4244.9, 29, "initVillageRepairer", "" },
		{ "fs_villager_generic", 5315.1, 78.5, -4017.7, 3, "initVillageRepairer", "" },
		{ "fs_villager_generic", 5357, 78.5, -4021.9, -147, "initVillageRepairer", "" },
		{ "fs_villager_generic", 5372.55, 78.5, -4030.8, -108, "initVillageRepairer", "" },
		{ "fs_villager_generic", 5362.4, 78.5, -4025.35, -142, "initVillageRepairer", "" },
		{ "fs_villager_generic", 5317.5, 78.5, -4011.25, -152, "initVillageRepairer", "" },
		{ "fs_villager_generic", 5176.85, 78.5, -4113.3, -138, "", "" },
		{ "fs_villager_generic", 5173.5, 78.5, -4117.45, 36, "", "" },
		{ "fs_villager_generic", 5218, 78.5, -4190.5, 88, "", "" },
		{ "fs_villager_generic", 5255.2, 78.5, -4225.3, -102, "", "" },
		{ "fs_villager_generic", 5322.35, 78.5, -4205.6, -95, "", "" },
		{ "fs_villager_generic", 5197.7, 78.5, -4071.7, 122, "", "fsVillageConvo7Template" },
		{ "fs_villager_generic", 5367.95, 78.5, -4054.45, -92, "", "" },
		{ "fs_villager_generic", 5367.95, 78.5, -4090.6, -110, "", "" },
		{ "fs_villager_generic", 5355.8, 78.5, -4132.85, 165, "", "" },
		{ "fs_villager_generic", 5412.05, 78.5, -4158.65, -36, "", "" },
		{ "fs_villager_generic", 5408.6, 78.5, -4198.55, -90, "", "" },
	},
	[3] = {},
	[4] = {},
}

villageObjectSpawns = {
	[0] = {
		{ "object/static/structure/general/streetlamp_large_style_02.iff", 5298.7, 78.5, -4201, 10.9 },
		{ "object/static/structure/general/streetlamp_large_style_02.iff", 5233.7, 78.5, -4191.6, 45.25 },
		{ "object/static/structure/general/streetlamp_large_style_02.iff", 5255.9, 78.5, -4239.2, 19.5 },
		{ "object/static/structure/general/streetlamp_large_style_02.iff", 5363.9, 78.5, -4176.4, 154.25 },
		{ "object/static/structure/general/streetlamp_large_style_02.iff", 5274.7, 78.5, -4143.8, 77.1 },
		{ "object/static/structure/general/streetlamp_large_style_02.iff", 5206.4, 78.5, -4133.1, 117 },
		{ "object/static/structure/general/streetlamp_large_style_02.iff", 5177.8, 78.5, -4140.8, 119.25 },
		{ "object/static/structure/general/streetlamp_large_style_02.iff", 5186, 78.5, -4201.9, 42.4 },
		{ "object/static/structure/general/streetlamp_large_style_02.iff", 5421.6, 78.5, -4123.9, 155.35 },
		{ "object/static/structure/general/streetlamp_large_style_02.iff", 5397.6, 78.5, -4071.8, 52.7 },
		{ "object/static/structure/general/streetlamp_large_style_02.iff", 5282.5, 78.5, -4018, 176.55 },
		{ "object/static/structure/general/streetlamp_large_style_02.iff", 5152.7, 78.64, -4114.1, 98.1 },
	},

	[1] = {
		{ "object/static/structure/dantooine/dant_large_communal_dest.iff", 5227.9, 78.5, -4171.6, -40 },
		{ "object/static/structure/dantooine/dant_large_rock_hut_dest_01.iff", 5303.8, 78.5, -4058, -55 },
		{ "object/static/structure/dantooine/dant_large_rock_hut_dest_02.iff", 5316.4, 78.5, -4024, 45 },
		{ "object/static/structure/dantooine/dant_large_rock_hut_dest_03.iff", 5311.5, 78.5, -4075.2, 55 },
		{ "object/static/structure/dantooine/dant_large_rock_hut_dest_01.iff", 5362.5, 78.5, -4042.8, 128 },
		{ "object/static/structure/corellia/corl_tent_large.iff", 5254.6, 78.5, -4029, -12 },
		{ "object/static/structure/general/camp_lawn_chair_s01.iff", 5250.9, 78.5, -4031.9, 110 },
		{ "object/static/structure/general/camp_lawn_chair_s01.iff", 5253.35, 78.5, -4030.6, -166 },
		{ "object/static/structure/general/poi_all_construction_stone_pile.iff", 5241.65, 78.5, -4029.9, 80 },
		{ "object/static/structure/tatooine/pillar_ruined_large_style_01.iff", 5356.2, 78.5, -4023.1, 29 },
		{ "object/static/structure/tatooine/pillar_ruined_large_style_01.iff", 5257.4, 78.5, -4241, 14 },
		{ "object/static/structure/tatooine/wall_gate_tatooine_style_02.iff", 5351.6, 78.5, -4021.4, -68 },
		{ "object/static/structure/tatooine/wall_ruined_tatooine_large_style_01.iff", 5361.2, 78.5, -4027.3, 122 },
		{ "object/static/structure/tatooine/wall_ruined_tatooine_large_style_02.iff", 5285.2, 78.5, -4242, 0 },
	},
	[2] = {
		{ "object/static/structure/corellia/corl_tent_large.iff", 5254.55, 78.5, -4028.95, -12.6 },
		{ "object/static/structure/dantooine/dant_large_rock_hut_no_flora_05m.iff", 5314.1, 78.5, -4027.25, -17.41 },
		{ "object/static/structure/dantooine/dant_large_rock_hut_no_flora_05m.iff", 5358.3, 78.5, -4040.85, 64.55 },
		{ "object/static/structure/dantooine/dant_large_rock_hut_no_flora_05m.iff", 5316.3, 78.5, -4070.65, -118.43 },
		{ "object/static/structure/dantooine/dant_large_rock_hut_no_flora_05m.iff", 5300.7, 78.5, -4062.65, -156.25 },
		{ "object/static/structure/dantooine/dant_large_communal_no_flora_10m.iff", 5231.5, 78.5, -4172.45, -60 },
		{ "object/static/structure/general/camp_lawn_chair_s01.iff", 5250.9, 78.5, -4031.9, 109.62 },
		{ "object/static/structure/general/camp_lawn_chair_s01.iff", 5253.35, 78.5, -4030.6, -166.16 },
		{ "object/static/structure/general/poi_all_construction_stone_pile.iff", 5241.6, 78.5, -4029.92, 80.2 },
		{ "object/static/structure/tatooine/wall_gate_tatooine_style_02.iff", 5351.6, 78.5, -4021.35, -67.61 },
		{ "object/static/structure/tatooine/wall_ruined_tatooine_large_style_01.iff", 5361.2, 78.5, -4027.29, 121.65 },
		{ "object/static/structure/tatooine/pillar_ruined_large_style_01.iff", 5356.2, 78.5, -4023.1, 28.65 },
		{ "object/static/structure/tatooine/pillar_ruined_large_style_01.iff", 5257.4, 78.5, -4241, 13.75 },
		{ "object/static/structure/tatooine/wall_ruined_tatooine_large_style_02.iff", 5285.15, 78.5, -4242, 0 },
	},
	[3] = {},
	[4] = {},
}
