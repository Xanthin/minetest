/*
Minetest Valleys C
Copyright (C) Duane Robertson <duane@duanerobertson.com>

Based on Valleys Mapgen by Gael de Sailly
 (https://forum.minetest.net/viewtopic.php?f=9&t=11430)
and mapgen_v7 by kwolekr, Ryan Kwolek <kwolekr@minetest.net>.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3.0 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

GNU GENERAL PUBLIC LICENSE
Version 3, 29 June 2007

See http://www.gnu.org/licenses/gpl-3.0.en.html
*/


#include "mapgen.h"
#include "voxel.h"
#include "noise.h"
#include "mapblock.h"
#include "mapnode.h"
#include "map.h"
#include "content_sao.h"
#include "nodedef.h"
#include "voxelalgorithms.h"
#include "settings.h" // For g_settings
#include "emerge.h"
#include "dungeongen.h"
#include "cavegen.h"
#include "treegen.h"
#include "mg_biome.h"
#include "mg_ore.h"
#include "mg_decoration.h"
#include "mapgen_valleys.h"

#include "util/timetaker.h"
//#undef NDEBUG
//#include "assert.h"


FlagDesc flagdesc_mapgen_valleys[] = {
	{"fast", MG_VALLEYS_FASTER_TERRAIN},
	{"v7caves", MG_VALLEYS_V7_CAVES},
	{"lava", MG_VALLEYS_LAVA},
	{"groundwater", MG_VALLEYS_GROUND_WATER},
	{"profile", MG_VALLEYS_PROFILE},
	{NULL,        0}
};

///////////////////////////////////////////////////////////////////////////////


MapgenValleys::MapgenValleys(int mapgenid, MapgenParams *params, EmergeManager *emerge)
	: Mapgen(mapgenid, params, emerge)
{
	this->m_emerge = emerge;
	this->bmgr = emerge->biomemgr;

	//// amount of elements to skip for the next index
	//// for noise/height/biome maps (not vmanip)
	this->ystride = csize.X;
	this->zstride = csize.X * (csize.Y + 2);

	this->biomemap        = new u8[csize.X * csize.Z];
	this->heightmap       = new s16[csize.X * csize.Z];
	this->heatmap         = NULL;
	this->humidmap        = NULL;

	// This is only here for compatibility with v7 caves.
	this->ridge_heightmap = this->heightmap;

	MapgenValleysParams *sp = (MapgenValleysParams *)params->sparams;
	this->spflags = sp->spflags;
	
	// If fast is selected, generate v7 caves. Vmg caves are slow.
	if (spflags & MG_VALLEYS_FASTER_TERRAIN)
		spflags = spflags | MG_VALLEYS_V7_CAVES;

	river_size = (float)(sp->river_size) / 100;
	river_depth = sp->river_depth + 1;
	water_level = sp->water_level;
	altitude_chill = 3.6 / sp->altitude_chill;
	cave_size = (float)(sp->cave_size) / 100;
	lava_max_height = sp->lava_max_height;
	ground_water_frequency = fmin(sp->ground_water_frequency, 1) / 10000;
	lava_frequency = fmin(sp->lava_frequency, 1) / 10000;

	humidity_adjust = sp->humidity - 50;
	temperature_adjust = (sp->temperature / 50);

	//// Terrain noise
	noise_filler_depth    = new Noise(&sp->np_filler_depth,    seed, csize.X, csize.Z);
	noise_v7_caves_1 = new Noise(&sp->np_v7_caves_1, seed, csize.X, csize.Y + 2, csize.Z);
	noise_v7_caves_2 = new Noise(&sp->np_v7_caves_2, seed, csize.X, csize.Y + 2, csize.Z);

	//// Biome noise
	noise_heat           = new Noise(&sp->np_biome_heat,           seed, csize.X, csize.Z);
	noise_humidity       = new Noise(&sp->np_biome_humidity,       seed, csize.X, csize.Z);
	noise_heat_blend     = new Noise(&sp->np_biome_heat_blend,     seed, csize.X, csize.Z);
	noise_humidity_blend = new Noise(&sp->np_biome_humidity_blend, seed, csize.X, csize.Z);

	// VMG noises
	noise_terrain_height = new Noise(&sp->np_terrain_height, seed, csize.X, csize.Z);
	noise_rivers = new Noise(&sp->np_rivers, seed, csize.X, csize.Z);
	noise_valley_depth = new Noise(&sp->np_valley_depth, seed, csize.X, csize.Z);
	noise_valley_profile = new Noise(&sp->np_valley_profile, seed, csize.X, csize.Z);
	noise_inter_valley_slope = new Noise(&sp->np_inter_valley_slope, seed, csize.X, csize.Z);
	noise_inter_valley_fill = new Noise(&sp->np_inter_valley_fill, seed, csize.X, csize.Y, csize.Z);
	noise_caves_1 = new Noise(&sp->np_caves_1, seed, csize.X, csize.Y, csize.Z);
	noise_caves_2 = new Noise(&sp->np_caves_2, seed, csize.X, csize.Y, csize.Z);
	noise_caves_3 = new Noise(&sp->np_caves_3, seed, csize.X, csize.Y, csize.Z);
	noise_caves_4 = new Noise(&sp->np_caves_4, seed, csize.X, csize.Y, csize.Z);
	noise_lava_1 = new Noise(&sp->np_lava_1, seed, csize.X, csize.Y, csize.Z);
	noise_lava_2 = new Noise(&sp->np_lava_2, seed, csize.X, csize.Y, csize.Z);
	noise_water_1 = new Noise(&sp->np_water_1, seed, csize.X, csize.Y, csize.Z);
	noise_water_2 = new Noise(&sp->np_water_2, seed, csize.X, csize.Y, csize.Z);

	//// Resolve nodes to be used
	INodeDefManager *ndef = emerge->ndef;

	c_stone                = ndef->getId("mapgen_stone");
	c_water_source         = ndef->getId("mapgen_water_source");
	c_lava_source          = ndef->getId("mapgen_lava_source");
	c_desert_stone         = ndef->getId("mapgen_desert_stone");
	c_ice                  = ndef->getId("mapgen_ice");
	c_sandstone            = ndef->getId("mapgen_sandstone");
	c_river_water_source   = ndef->getId("mapgen_river_water_source");
	c_sand                 = ndef->getId("mapgen_sand");

	c_cobble               = ndef->getId("mapgen_cobble");
	c_stair_cobble         = ndef->getId("mapgen_stair_cobble");
	c_mossycobble          = ndef->getId("mapgen_mossycobble");
	c_sandstonebrick       = ndef->getId("mapgen_sandstonebrick");
	c_stair_sandstonebrick = ndef->getId("mapgen_stair_sandstonebrick");

	if (c_ice == CONTENT_IGNORE)
		c_ice = CONTENT_AIR;
	if (c_mossycobble == CONTENT_IGNORE)
		c_mossycobble = c_cobble;
	if (c_stair_cobble == CONTENT_IGNORE)
		c_stair_cobble = c_cobble;
	if (c_sandstonebrick == CONTENT_IGNORE)
		c_sandstonebrick = c_sandstone;
	if (c_stair_sandstonebrick == CONTENT_IGNORE)
		c_stair_sandstonebrick = c_sandstone;
	if (c_river_water_source == CONTENT_IGNORE)
		c_river_water_source = c_water_source;
	if (c_sand == CONTENT_IGNORE)
		c_sand = c_stone;
}


MapgenValleys::~MapgenValleys()
{
	delete noise_filler_depth;

	delete noise_heat;
	delete noise_humidity;
	delete noise_heat_blend;
	delete noise_humidity_blend;

	delete noise_terrain_height;
	delete noise_rivers;
	delete noise_valley_depth;
	delete noise_valley_profile;
	delete noise_inter_valley_slope;
	delete noise_inter_valley_fill;
	delete noise_v7_caves_1;
	delete noise_v7_caves_2;
	delete noise_caves_1;
	delete noise_caves_2;
	delete noise_caves_3;
	delete noise_caves_4;
	delete noise_lava_1;
	delete noise_lava_2;
	delete noise_water_1;
	delete noise_water_2;

	delete[] heightmap;
	delete[] biomemap;
}


MapgenValleysParams::MapgenValleysParams()
{
	spflags = MG_VALLEYS_V7_CAVES;

	np_filler_depth = NoiseParams(0, 1.2, v3f(150, 150, 150), 261, 3, 0.7, 2.0);
	np_v7_caves_1 = NoiseParams(0, 12, v3f(100, 100, 100), 52534, 4, 0.5, 2.0);
	np_v7_caves_2 = NoiseParams(0, 12, v3f(100, 100, 100), 10325, 4, 0.5, 2.0);

	np_biome_heat = NoiseParams(50, 50, v3f(750.0, 750.0, 750.0), 5349, 3, 0.5, 2.0);
	np_biome_heat_blend = NoiseParams(0, 1.5, v3f(8.0, 8.0, 8.0), 13, 2, 1.0, 2.0);
	np_biome_humidity = NoiseParams(50, 50, v3f(750.0, 750.0, 750.0), 842, 3, 0.5, 2.0);
	np_biome_humidity_blend = NoiseParams(0, 1.5, v3f(8.0, 8.0, 8.0), 90003, 2, 1.0, 2.0);

	// vmg noises
	np_terrain_height = NoiseParams(-10, 50, v3f(1024, 1024, 1024), 5202, 6, 0.4, 2.0);
	np_rivers = NoiseParams(0, 1, v3f(256, 256, 256), -6050, 5, 0.6, 2.0);
	np_valley_depth = NoiseParams(5, 4, v3f(512, 512, 512), -1914, 1, 1.0, 2.0);
	np_valley_profile = NoiseParams(0.6, 0.5, v3f(512, 512, 512), 777, 1, 1.0, 2.0);
	np_inter_valley_slope = NoiseParams(0.5, 0.5, v3f(128, 128, 128), 746, 1, 1.0, 2.0);
	np_inter_valley_fill = NoiseParams(0, 1, v3f(256, 256, 256), 1993, 6, 0.8, 2.0);
	np_caves_1 = NoiseParams(0, 1, v3f(32, 32, 32), -4640, 4, 0.5, 2.0);
	np_caves_2 = NoiseParams(0, 1, v3f(32, 32, 32), 8804, 4, 0.5, 2.0);
	np_caves_3 = NoiseParams(0, 1, v3f(32, 32, 32), -4780, 4, 0.5, 2.0);
	np_caves_4 = NoiseParams(0, 1, v3f(32, 32, 32), -9969, 4, 0.5, 2.0);
	np_lava_1 = NoiseParams(0, 1, v3f(32, 32, 32), 75266, 4, 0.5, 2.0);
	np_lava_2 = NoiseParams(0, 1, v3f(32, 32, 32), -89113, 4, 0.5, 2.0);
	np_water_1 = NoiseParams(0, 1, v3f(32, 32, 32), 82329, 4, 0.5, 2.0);
	np_water_2 = NoiseParams(0, 1, v3f(32, 32, 32), -59107, 4, 0.5, 2.0);
}


void MapgenValleysParams::readParams(const Settings *settings)
{
	settings->getNoiseParams("mg_valleys_np_filler_depth",    np_filler_depth);
	settings->getNoiseParams("mg_valleys_np_v7_caves_1",    np_v7_caves_1);
	settings->getNoiseParams("mg_valleys_np_v7_caves_2",    np_v7_caves_2);
	settings->getNoiseParams("mg_valleys_np_biome_heat", np_biome_heat);
	settings->getNoiseParams("mg_valleys_np_biome_heat_blend", np_biome_heat_blend);
	settings->getNoiseParams("mg_valleys_np_biome_humidity", np_biome_humidity);
	settings->getNoiseParams("mg_valleys_np_biome_humidity_blend",    np_biome_humidity_blend);
	
	if (!settings->getS16NoEx("mg_valleys_temperature", temperature))
		temperature = 50;  // in Fahrenheit, unfortunately
	if (!settings->getS16NoEx("mg_valleys_humidity", humidity))
		humidity = 50;  // as a percentage
	if (!settings->getS16NoEx("mg_valleys_river_size", river_size))
		river_size = 5;  // How wide to make rivers.
	if (!settings->getS16NoEx("mg_valleys_river_depth", river_depth))
		river_depth = 4;  // How deep to carve river channels.
	if (!settings->getS16NoEx("mg_valleys_water_level", water_level))
		water_level = 1;  // Sea-level.
	if (!settings->getS16NoEx("mg_valleys_altitude_chill", altitude_chill))
		altitude_chill = 90;  // The altitude at which temperature drops by 20C.
	if (!settings->getS16NoEx("mg_valleys_cave_size", cave_size))
		cave_size = 7;  // Be careful. Caves get ugly when they start
	                        //  to collide with one another.
	if (!settings->getS16NoEx("mg_valleys_lava_max_height", lava_max_height))
		lava_max_height = 0;  // Lava will never be higher than this.
	if (!settings->getS16NoEx("mg_valleys_ground_water_frequency", ground_water_frequency))
		ground_water_frequency = 5;  // 
	if (!settings->getS16NoEx("mg_valleys_lava_frequency", lava_frequency))
		lava_frequency = 5;  // 

	// vmg noises
	settings->getNoiseParams("mg_valleys_np_terrain_height",    np_terrain_height);
	settings->getNoiseParams("mg_valleys_np_rivers",    np_rivers);
	settings->getNoiseParams("mg_valleys_np_valley_depth",    np_valley_depth);
	settings->getNoiseParams("mg_valleys_np_valley_profile",    np_valley_profile);
	settings->getNoiseParams("mg_valleys_np_inter_valley_slope",    np_inter_valley_slope);
	settings->getNoiseParams("mg_valleys_np_inter_valley_fill",    np_inter_valley_fill);
	settings->getNoiseParams("mg_valleys_np_caves_1",    np_caves_1);
	settings->getNoiseParams("mg_valleys_np_caves_2",    np_caves_2);
	settings->getNoiseParams("mg_valleys_np_caves_3",    np_caves_3);
	settings->getNoiseParams("mg_valleys_np_caves_4",    np_caves_4);
	settings->getNoiseParams("mg_valleys_np_lava_1",    np_lava_1);
	settings->getNoiseParams("mg_valleys_np_lava_2",    np_lava_2);
	settings->getNoiseParams("mg_valleys_np_water_1",    np_water_1);
	settings->getNoiseParams("mg_valleys_np_water_2",    np_water_2);
}


void MapgenValleysParams::writeParams(Settings *settings) const
{
	settings->setFlagStr("mg_valleys_spflags", spflags, flagdesc_mapgen_valleys, U32_MAX);

	settings->setNoiseParams("mg_valleys_np_filler_depth",    np_filler_depth);
	settings->setNoiseParams("mg_valleys_np_v7_caves_1",    np_v7_caves_1);
	settings->setNoiseParams("mg_valleys_np_v7_caves_2",    np_v7_caves_2);
	settings->setNoiseParams("mg_valleys_np_biome_heat", np_biome_heat);
	settings->setNoiseParams("mg_valleys_np_biome_heat_blend", np_biome_heat_blend);
	settings->setNoiseParams("mg_valleys_np_biome_humidity", np_biome_humidity);
	settings->setNoiseParams("mg_valleys_np_biome_humidity_blend",    np_biome_humidity_blend);
	
	settings->setS16("mg_valleys_temperature", temperature);
	settings->setS16("mg_valleys_humidity", humidity);
	settings->setS16("mg_valleys_river_size", river_size);
	settings->setS16("mg_valleys_river_depth", river_depth);
	settings->setS16("mg_valleys_water_level", water_level);
	settings->setS16("mg_valleys_altitude_chill", altitude_chill);
	settings->setS16("mg_valleys_cave_size", cave_size);
	settings->setS16("mg_valleys_lava_max_height", lava_max_height);
	settings->setS16("mg_valleys_lava_frequency", lava_frequency);
	settings->setS16("mg_valleys_ground_water_frequency", ground_water_frequency);

	// vmg noises
	settings->setNoiseParams("mg_valleys_np_terrain_height",    np_terrain_height);
	settings->setNoiseParams("mg_valleys_np_rivers",    np_rivers);
	settings->setNoiseParams("mg_valleys_np_valley_depth",    np_valley_depth);
	settings->setNoiseParams("mg_valleys_np_valley_profile",    np_valley_profile);
	settings->setNoiseParams("mg_valleys_np_inter_valley_slope",    np_inter_valley_slope);
	settings->setNoiseParams("mg_valleys_np_inter_valley_fill",    np_inter_valley_fill);
	settings->setNoiseParams("mg_valleys_np_caves_1",    np_caves_1);
	settings->setNoiseParams("mg_valleys_np_caves_2",    np_caves_2);
	settings->setNoiseParams("mg_valleys_np_caves_3",    np_caves_3);
	settings->setNoiseParams("mg_valleys_np_caves_4",    np_caves_4);
	settings->setNoiseParams("mg_valleys_np_lava_1",    np_lava_1);
	settings->setNoiseParams("mg_valleys_np_lava_2",    np_lava_2);
	settings->setNoiseParams("mg_valleys_np_water_1",    np_water_1);
	settings->setNoiseParams("mg_valleys_np_water_2",    np_water_2);
}


///////////////////////////////////////


void MapgenValleys::makeChunk(BlockMakeData *data)
{
	// Pre-conditions
	assert(data->vmanip);
	assert(data->nodedef);
	assert(data->blockpos_requested.X >= data->blockpos_min.X &&
		data->blockpos_requested.Y >= data->blockpos_min.Y &&
		data->blockpos_requested.Z >= data->blockpos_min.Z);
	assert(data->blockpos_requested.X <= data->blockpos_max.X &&
		data->blockpos_requested.Y <= data->blockpos_max.Y &&
		data->blockpos_requested.Z <= data->blockpos_max.Z);

	this->generating = true;
	this->vm   = data->vmanip;
	this->ndef = data->nodedef;

	TimeTaker t("makeChunk");

	v3s16 blockpos_min = data->blockpos_min;
	v3s16 blockpos_max = data->blockpos_max;
	node_min = blockpos_min * MAP_BLOCKSIZE;
	node_max = (blockpos_max + v3s16(1, 1, 1)) * MAP_BLOCKSIZE - v3s16(1, 1, 1);
	full_node_min = (blockpos_min - 1) * MAP_BLOCKSIZE;
	full_node_max = (blockpos_max + 2) * MAP_BLOCKSIZE - v3s16(1, 1, 1);

	blockseed = getBlockSeed2(full_node_min, seed);

	// Generate noise maps and base terrain height.
	calculateNoise();

	// Generate base terrain with initial heightmaps
	s16 stone_surface_max_y = generateTerrain();

	// Create biomemap at heightmap surface
	calcBiomes(csize.X, csize.Z, heatmap, humidmap, heightmap, biomemap);

	// Actually place the biome-specific nodes
	MgStoneType stone_type = generateBiomes(heatmap, humidmap);

	if (flags & MG_CAVES) {
		if (spflags & MG_VALLEYS_V7_CAVES)
			generateCaves(stone_surface_max_y);
		else
			generateVmgCaves(stone_surface_max_y);
	}

	if ((flags & MG_DUNGEONS) && (stone_surface_max_y >= node_min.Y)) {
		DungeonParams dp;

		dp.np_rarity  = nparams_dungeon_rarity;
		dp.np_density = nparams_dungeon_density;
		dp.np_wetness = nparams_dungeon_wetness;
		dp.c_water    = c_water_source;
		if (stone_type == STONE) {
			dp.c_cobble = c_cobble;
			dp.c_moss   = c_mossycobble;
			dp.c_stair  = c_stair_cobble;

			dp.diagonal_dirs = false;
			dp.mossratio     = 3.0;
			dp.holesize      = v3s16(1, 2, 1);
			dp.roomsize      = v3s16(0, 0, 0);
			dp.notifytype    = GENNOTIFY_DUNGEON;
		} else if (stone_type == DESERT_STONE) {
			dp.c_cobble = c_desert_stone;
			dp.c_moss   = c_desert_stone;
			dp.c_stair  = c_desert_stone;

			dp.diagonal_dirs = true;
			dp.mossratio     = 0.0;
			dp.holesize      = v3s16(2, 3, 2);
			dp.roomsize      = v3s16(2, 5, 2);
			dp.notifytype    = GENNOTIFY_TEMPLE;
		} else if (stone_type == SANDSTONE) {
			dp.c_cobble = c_sandstonebrick;
			dp.c_moss   = c_sandstonebrick;
			dp.c_stair  = c_sandstonebrick;

			dp.diagonal_dirs = false;
			dp.mossratio     = 0.0;
			dp.holesize      = v3s16(2, 2, 2);
			dp.roomsize      = v3s16(2, 0, 2);
			dp.notifytype    = GENNOTIFY_DUNGEON;
		}

		DungeonGen dgen(this, &dp);
		dgen.generate(blockseed, full_node_min, full_node_max);
	}

	// Correct problems with the rivers.
	fixRivers(csize.X, csize.Z, heightmap);

	// Generate the registered decorations
	m_emerge->decomgr->placeAllDecos(this, blockseed, node_min, node_max);

	// Generate the registered ores
	m_emerge->oremgr->placeAllOres(this, blockseed, node_min, node_max);

	// Sprinkle some dust on top after everything else was generated
	dustTopNodes();

	TimeTaker tll("liquid_lighting");

	updateLiquid(&data->transforming_liquid, full_node_min, full_node_max);

	if (flags & MG_LIGHT)
		calcLighting(node_min - v3s16(0, 1, 0), node_max + v3s16(0, 1, 0),
			full_node_min, full_node_max);

	if (MG_VALLEYS_PROFILE)
		printf("liquid_lighting: %dms\n", tll.stop());

	if (MG_VALLEYS_PROFILE)
		printf("makeChunk: %dms\n", t.stop());

	this->generating = false;
}


void MapgenValleys::calcBiomes(s16 sx, s16 sy, float *heat_map, float *humidity_map, s16 *height_map, u8 *biomeid_map)
{
	for (s32 index = 0; index < sx * sy; index++) {
		// Both heat and humidity have already been adjusted for altitude.
		Biome *biome = bmgr->getBiome(heat_map[index], humidity_map[index], height_map[index]);
		biomeid_map[index] = biome->index;
	}
}


void MapgenValleys::fixRivers(s16 sx, s16 sy, s16 *height_map)
{
	MapNode n_air(CONTENT_AIR);

	s16 index = 0;
	for (s16 z = node_min.Z; z <= node_max.Z; z++)
		for (s16 x = node_min.X; x <= node_max.X; x++, index++) {
			s16 river_y = (s16) noise_rivers->result[index];
			if (river_y > 0 && river_y >= node_min.Y && river_y <= node_max.Y) {
				// Try to eliminate rivers floating over cave openings.
				bool supported = false;
				for (s16 y = river_y; y >= node_min.Y; y--) {
					u32 i = vm->m_area.index(x, y, z);
					content_t c = vm->m_data[i].getContent();
					if (c == CONTENT_AIR)
						break;
					if (c != c_river_water_source) {
						supported = true;
						break;
					}
				}
				if (!supported) {
					for (s16 y = river_y; y >= node_min.Y; y--) {
						u32 i = vm->m_area.index(x, y, z);
						content_t c = vm->m_data[i].getContent();
						if (c == c_river_water_source)
							vm->m_data[i] = n_air;
						else
							break;
					}
				}

				if (!supported)
					continue;

				// Try to keep the rivers from overflowing.
				// Look at the neighbor heights.
				for (s16 z1 = -2; z1 <= 2; z1++)
					for (s16 x1 = -2; x1 <= 2; x1++) {
						if (z+z1 >= node_min.Z and z+z1 <= node_max.Z and x+x1 >= node_min.X and x+x1 <= node_max.X) {
							s16 i2 = index + z1 * csize.X + x1;
							float river_y2 = noise_rivers->result[i2];

							if (river_y2 < 1 && height_map[i2] < river_y)
								// Lower the river.
								for (s16 y = river_y; y > height_map[i2]; y--)
									if (y >= node_min.Y && y <= node_max.Y) {
										u32 i = vm->m_area.index(x, y, z);
										vm->m_data[i] = n_air;
									}
						}
					}
			}
		}
}


void MapgenValleys::calculateNoise()
{
	//TimeTaker t("calculateNoise", NULL, PRECISION_MICRO);
	int x = node_min.X;
	int y = node_min.Y - 1;
	int z = node_min.Z;

	TimeTaker tcn("actualNoise");

	noise_filler_depth->perlinMap2D(x, z);
	noise_heat->perlinMap2D(x, z);
	noise_humidity->perlinMap2D(x, z);
	noise_heat_blend->perlinMap2D(x, z);
	noise_humidity_blend->perlinMap2D(x, z);

	// vmg noises
	noise_terrain_height->perlinMap2D(x, z);
	noise_rivers->perlinMap2D(x, z);
	noise_valley_depth->perlinMap2D(x, z);
	noise_valley_profile->perlinMap2D(x, z);
	noise_inter_valley_slope->perlinMap2D(x, z);

	if (flags & MG_CAVES) {
		if (spflags & MG_VALLEYS_V7_CAVES) {
			noise_v7_caves_1->perlinMap3D(x, y, z);
			noise_v7_caves_2->perlinMap3D(x, y, z);
		} else {
			// Way too many noise maps.
			noise_caves_1->perlinMap3D(x, y, z);
			noise_caves_2->perlinMap3D(x, y, z);
			noise_caves_3->perlinMap3D(x, y, z);
			noise_caves_4->perlinMap3D(x, y, z);

			if (spflags & MG_VALLEYS_LAVA) {
				noise_lava_1->perlinMap2D(x, z);
				noise_lava_2->perlinMap3D(x, y, z);
			}
			if (spflags & MG_VALLEYS_GROUND_WATER) {
				noise_water_1->perlinMap2D(x, z);
				noise_water_2->perlinMap3D(x, y, z);
			}
		}
	}

	if (MG_VALLEYS_PROFILE)
		printf("actualNoise: %dms\n", tcn.stop());

	for (s32 index = 0; index < csize.X * csize.Z; index++) {
		noise_heat->result[index] += noise_heat_blend->result[index];
		noise_heat->result[index] *= temperature_adjust;
		noise_humidity->result[index] += noise_humidity_blend->result[index];
	}

	float mount, valley;
	u32 index = 0;
	for (s16 zi = node_min.Z; zi <= node_max.Z; zi++)
	for (s16 xi = node_min.X; xi <= node_max.X; xi++, index++) {
		float terrain_height = noise_terrain_height->result[index];
		float rivers = noise_rivers->result[index];
		float valley_depth = noise_valley_depth->result[index];
		float valley_profile = noise_valley_profile->result[index];
		float inter_valley_slope = noise_inter_valley_slope->result[index];

		// The two parameters that we actually need to generate terrain
		//  are terrain height and river noise.
		mount = baseGroundFromNoise(xi, zi, valley_depth, terrain_height, &rivers, valley_profile, inter_valley_slope, &valley);
		noise_terrain_height->result[index] = mount;
		noise_rivers->result[index] = rivers;

		// Assign the humidity adjusted by water proximity (and thus altitude).
		// I can't think of a reason why a mod would expect base humidity
		//  from noise or at any altitude other than ground level.
		noise_humidity->result[index] = humidityByTerrain(noise_humidity->result[index], mount, valley);

		// Assign the heat adjusted by altitude. See humidity, above.
		if (mount > 0)
			noise_heat->result[index] -= altitude_chill * mount;
	}

	heatmap = noise_heat->result;
	humidmap = noise_humidity->result;
}


// This ugly function keeps me from having to maintain two similar sets of
//  complicated code to determine ground level.
float MapgenValleys::baseGroundFromNoise(s16 x, s16 z, float valley_depth, float terrain_height, float *rivers, float valley_profile, float inter_valley_slope, float *valley)
{
	// The square function changes the behaviour of this noise:
	//  very often small, and sometimes very high.
	float valley_d = pow(valley_depth, 2);
	// valley_d is here because terrain is generally higher where valleys
	//  are deep (mountains). base represents the height of the
	//  rivers, most of the surface is above.
	float base = terrain_height + valley_d;
	// "river" represents the distance from the river, in arbitrary units.
	float river = fabs(*rivers) - river_size;
	*rivers = -31000;
	// Use the curve of the function 1−exp(−(x/a)²) to model valleys.
	//  Making "a" vary (0 < a ≤ 1) changes the shape of the valleys.
	//  Try it with a geometry software !
	//   (here x = "river" and a = valley_profile).
	//  "valley" represents the height of the terrain, from the rivers.
	*valley = valley_d * (1 - exp(- pow(river / valley_profile, 2)));
	// approximate height of the terrain at this point
	//  (could be slightly modified by the 3D noise #6)
	float mount = base + *valley;

	float slope = *valley * inter_valley_slope;
	// Rivers are placed where "river" is negative, so where the original
	//  noise value is close to zero.
	if (river < 0) {
		// Use the the function −sqrt(1−x²) which models a circle.
		float depth = (river_depth * sqrt(1 - pow((river / river_size + 1), 2))) + 1;
		// This seems to be behaving differently than in the lua.
		*rivers = base;
		// base - depth : height of the bottom of the river
		// water_level - 2 : don't make rivers below 2 nodes under the surface
		mount = fmin(fmax(mount - depth, water_level - 2), mount);
		// Slope has no influence on rivers.
		slope = 0;
	}

	// This doesn't make a huge difference in appearence,
	//  so it might be worth leaving out.
	if (!(spflags & MG_VALLEYS_FASTER_TERRAIN)) {
		float mount2 = mount;
		// Find the final ground level, influenced by inter_valley_slope.
		if (NoisePerlin3D(&noise_inter_valley_fill->np, x, mount2, z, seed) * slope > mount2 - mount) {
			mount2++;
			while (NoisePerlin3D(&noise_inter_valley_fill->np, x, mount2, z, seed) * slope > mount2 - mount)
				mount2++;
		} else {
			mount2--;
			while (NoisePerlin3D(&noise_inter_valley_fill->np, x, mount2, z, seed) * slope <= mount2 - mount)
				mount2--;
		}
		// This affects rivers too.
		*rivers += mount2 - mount;
		mount = mount2;
	}

	return mount;
}


float MapgenValleys::humidityByTerrain(float humidity, float mount, float valley)
{
	humidity += humidity_adjust;
	if (mount > water_level) {
		// humidity is usually higher near water.
		float sea_water = pow(0.5, fmax((mount - water_level) / 6, 0));
		float river_water = pow(0.5, fmax(valley / 3, 0));
		sea_water /= 2;
		float water = sea_water + (1 - sea_water) * river_water;
		humidity = fmax(humidity, (65 * water));
	}

	return humidity;
}


Biome *MapgenValleys::getBiomeAtPoint(v3s16 p)
{
	float heat = NoisePerlin2D(&noise_heat->np, p.X, p.Z, seed) + NoisePerlin2D(&noise_heat_blend->np, p.X, p.Z, seed);
	heat -= altitude_chill * p.Y;

	float terrain_height = NoisePerlin2D(&noise_terrain_height->np, p.X, p.Z, seed);
	float rivers = NoisePerlin2D(&noise_rivers->np, p.X, p.Z, seed);
	float valley_depth = NoisePerlin2D(&noise_valley_depth->np, p.X, p.Z, seed);
	float valley_profile = NoisePerlin2D(&noise_valley_profile->np, p.X, p.Z, seed);
	float inter_valley_slope = NoisePerlin2D(&noise_inter_valley_slope->np, p.X, p.Z, seed);
	float valley;

	float mount = baseGroundFromNoise(p.X, p.Z, valley_depth, terrain_height, &rivers, valley_profile, inter_valley_slope, &valley);
	s16 groundlevel = (s16) mount;

	float humidity = NoisePerlin2D(&noise_humidity->np, p.X, p.Z, seed) + NoisePerlin2D(&noise_humidity_blend->np, p.X, p.Z, seed);
	humidity = humidityByTerrain(humidity, mount, valley);

	return bmgr->getBiome(heat, humidity, groundlevel);
}

int MapgenValleys::getGroundLevelAtPoint(v2s16 p)
{
	// Base terrain calculation
	s16 y = baseTerrainLevelAtPoint(p.X, p.Y);

	return y;
}


float MapgenValleys::baseTerrainLevelAtPoint(s16 x, s16 z)
{
	float terrain_height = NoisePerlin2D(&noise_terrain_height->np, x, z, seed);
	float rivers = NoisePerlin2D(&noise_rivers->np, x, z, seed);
	float valley_depth = NoisePerlin2D(&noise_valley_depth->np, x, z, seed);
	float valley_profile = NoisePerlin2D(&noise_valley_profile->np, x, z, seed);
	float inter_valley_slope = NoisePerlin2D(&noise_inter_valley_slope->np, x, z, seed);
	float valley;

	return MapgenValleys::baseGroundFromNoise(x, z, valley_depth, terrain_height, &rivers, valley_profile, inter_valley_slope, &valley);
}


int MapgenValleys::generateTerrain()
{
	MapNode n_air(CONTENT_AIR);
	MapNode n_stone(c_stone);
	MapNode n_water(c_water_source);
	MapNode n_river_water(c_river_water_source);
	MapNode n_sand(c_sand);

	v3s16 em = vm->m_area.getExtent();
	s16 surface_min_y = MAX_MAP_GENERATION_LIMIT;
	s16 surface_max_y = -MAX_MAP_GENERATION_LIMIT;
	u32 index = 0;

	for (s16 z = node_min.Z; z <= node_max.Z; z++)
	for (s16 x = node_min.X; x <= node_max.X; x++, index++) {
		s16 river_y = (s16) noise_rivers->result[index];
		s16 surface_y = (s16) noise_terrain_height->result[index];

		heightmap[index] = surface_y;

		if (surface_y < surface_min_y)
			surface_min_y = surface_y;

		if (surface_y > surface_max_y)
			surface_max_y = surface_y;

		// Mapgens concern themselves with stone and water.
		u32 i = vm->m_area.index(x, node_min.Y - 1, z);
		for (s16 y = node_min.Y - 1; y <= node_max.Y + 1; y++) {
			if (vm->m_data[i].getContent() == CONTENT_IGNORE) {
				if (river_y > surface_y && y == surface_y + 1)
					// river bottom
					vm->m_data[i] = n_sand;
				else if (y <= surface_y)
					// ground
					vm->m_data[i] = n_stone;
				else if (river_y > surface_y && y <= river_y)
					// river
					vm->m_data[i] = n_river_water;
				else if (y <= water_level)
					// sea
					vm->m_data[i] = n_water;
				else
					vm->m_data[i] = n_air;
			}
			vm->m_area.add_y(em, i, 1);
		}
	}

	return surface_max_y;
}


MgStoneType MapgenValleys::generateBiomes(float *heat_map, float *humidity_map)
{
	v3s16 em = vm->m_area.getExtent();
	u32 index = 0;
	MgStoneType stone_type = STONE;

	for (s16 z = node_min.Z; z <= node_max.Z; z++)
	for (s16 x = node_min.X; x <= node_max.X; x++, index++) {
		Biome *biome = NULL;
		u16 depth_top = 0;
		u16 base_filler = 0;
		u16 depth_water_top = 0;
		u32 vi = vm->m_area.index(x, node_max.Y, z);

		// Check node at base of mapchunk above, either a node of a previously
		// generated mapchunk or if not, a node of overgenerated base terrain.
		content_t c_above = vm->m_data[vi + em.X].getContent();
		bool air_above = c_above == CONTENT_AIR;
		bool water_above = (c_above == c_water_source);

		// If there is air or water above enable top/filler placement, otherwise force
		// nplaced to stone level by setting a number exceeding any possible filler depth.
		u16 nplaced = (air_above || water_above) ? 0 : U16_MAX;

		for (s16 y = node_max.Y; y >= node_min.Y; y--) {
			content_t c = vm->m_data[vi].getContent();

			// Biome is recalculated each time an upper surface is detected while
			// working down a column. The selected biome then remains in effect for
			// all nodes below until the next surface and biome recalculation.
			// Biome is recalculated:
			// 1. At the surface of stone below air or water.
			// 2. At the surface of water below air.
			// 3. When stone or water is detected but biome has not yet been calculated.
			if ((c == c_stone && (air_above || water_above || !biome)) ||
					((c == c_water_source || c == c_river_water_source) && (air_above || !biome))) {
				// Both heat and humidity have already been adjusted for altitude.
				biome = bmgr->getBiome(heat_map[index], humidity_map[index], y);
#if 0
				if (biome->index != biomemap[index])
					printf("Biome change at %d,%d\n", x,z);
				else
					printf("No biome change at %d,%d\n", x,z);
#endif
				depth_top = biome->depth_top;
				base_filler = MYMAX(depth_top + biome->depth_filler + noise_filler_depth->result[index], 0);
				depth_water_top = biome->depth_water_top;

				// Detect stone type for dungeons during every biome calculation.
				// This is more efficient than detecting per-node and will not
				// miss any desert stone or sandstone biomes.
				if (biome->c_stone == c_desert_stone)
					stone_type = DESERT_STONE;
				else if (biome->c_stone == c_sandstone)
					stone_type = SANDSTONE;
			}

			if (c == c_stone) {
				content_t c_below = vm->m_data[vi - em.X].getContent();

				// If the node below isn't solid, make this node stone, so that
				// any top/filler nodes above are structurally supported.
				// This is done by aborting the cycle of top/filler placement
				// immediately by forcing nplaced to stone level.
				if (c_below == CONTENT_AIR || c_below == c_water_source || c_below == c_river_water_source)
					nplaced = U16_MAX;

				if (nplaced < depth_top) {
					vm->m_data[vi] = MapNode(biome->c_top);
					nplaced++;
				} else if (nplaced < base_filler) {
					vm->m_data[vi] = MapNode(biome->c_filler);
					nplaced++;
				} else {
					vm->m_data[vi] = MapNode(biome->c_stone);
				}

				air_above = false;
				water_above = false;
			} else if (c == c_water_source) {
				vm->m_data[vi] = MapNode((y > (s32)(water_level - depth_water_top)) ? biome->c_water_top : biome->c_water);
				nplaced = 0;  // Enable top/filler placement for next surface
				air_above = false;
				water_above = true;
			} else if (c == c_river_water_source) {
				vm->m_data[vi] = MapNode(biome->c_river_water);
				nplaced = U16_MAX;
				air_above = false;
				water_above = true;
			} else if (c == CONTENT_AIR) {
				nplaced = 0;  // Enable top/filler placement for next surface
				air_above = true;
				water_above = false;
			} else {  // Possible various nodes overgenerated from neighbouring mapchunks
				nplaced = U16_MAX;  // Disable top/filler placement
				air_above = false;
				water_above = false;
			}

			vm->m_area.add_y(em, vi, -1);
		}
	}

	return stone_type;
}


void MapgenValleys::dustTopNodes()
{
	if (node_max.Y < water_level)
		return;

	v3s16 em = vm->m_area.getExtent();
	u32 index = 0;

	for (s16 z = node_min.Z; z <= node_max.Z; z++)
	for (s16 x = node_min.X; x <= node_max.X; x++, index++) {
		Biome *biome = (Biome *)bmgr->getRaw(biomemap[index]);

		if (biome->c_dust == CONTENT_IGNORE)
			continue;

		u32 vi = vm->m_area.index(x, full_node_max.Y, z);
		content_t c_full_max = vm->m_data[vi].getContent();
		s16 y_start;

		if (c_full_max == CONTENT_AIR) {
			y_start = full_node_max.Y - 1;
		} else if (c_full_max == CONTENT_IGNORE) {
			vi = vm->m_area.index(x, node_max.Y + 1, z);
			content_t c_max = vm->m_data[vi].getContent();

			if (c_max == CONTENT_AIR)
				y_start = node_max.Y;
			else
				continue;
		} else {
			continue;
		}

		vi = vm->m_area.index(x, y_start, z);
		for (s16 y = y_start; y >= node_min.Y - 1; y--) {
			if (vm->m_data[vi].getContent() != CONTENT_AIR)
				break;

			vm->m_area.add_y(em, vi, -1);
		}

		content_t c = vm->m_data[vi].getContent();
		if (!ndef->get(c).buildable_to && c != CONTENT_IGNORE && c != biome->c_dust) {
			vm->m_area.add_y(em, vi, 1);
			vm->m_data[vi] = MapNode(biome->c_dust);
		}
	}
}


void MapgenValleys::generateVmgCaves(s16 max_stone_y)
{
	if (max_stone_y >= node_min.Y) {
		u32 index = 0;

		// Add ground water and lava.
		u32 index2 = 0;
		u32 ph = 0;
		float water_chance = ground_water_frequency;
		float lava_chance = ((1 - ((node_min.Y - lava_max_height) / 2000)) * lava_frequency);
		// Since there's a 3D and a 2D noise, they need two indices.
		if ((spflags & MG_VALLEYS_GROUND_WATER) || (spflags & MG_VALLEYS_LAVA)) {
			for (s16 z = node_min.Z; z <= node_max.Z; z++) {
				ph = index2;
				for (s16 y = node_min.Y; y <= node_max.Y; y++) {
					index2 = ph; 
					for (s16 x = node_min.X; x <= node_max.X; x++, index++, index2++) {
						if ((spflags & MG_VALLEYS_GROUND_WATER) && y < water_level && pow(noise_water_1->result[index2], 2) + pow(noise_water_2->result[index], 2) < water_chance) {
							u32 i = vm->m_area.index(x, y, z);
							content_t c = vm->m_data[i].getContent();
							if (c == c_stone)
								vm->m_data[i] = MapNode(c_river_water_source);
						}
						if ((spflags & MG_VALLEYS_LAVA) && y < lava_max_height && pow(noise_lava_1->result[index2], 2) + fabs(noise_lava_2->result[index]) < lava_chance) {
							u32 i = vm->m_area.index(x, y, z);
							content_t c = vm->m_data[i].getContent();
							if (c == c_stone)
								vm->m_data[i] = MapNode(c_lava_source);
						}
					}
				}
			}
		}

		// Empty out the caves and take care of ceiling and floor.
		index = 0;
		for (s16 z = node_min.Z; z <= node_max.Z; z++)
			for (s16 y = node_min.Y; y <= node_max.Y; y++)
				for (s16 x = node_min.X; x <= node_max.X; x++, index++) {
					u32 i = vm->m_area.index(x, y, z);
					content_t c = vm->m_data[i].getContent();

					// Caves occur where the noises approach zero.
					if (pow(noise_caves_1->result[index], 2) + pow(noise_caves_2->result[index], 2) + pow(noise_caves_3->result[index], 2) + pow(noise_caves_4->result[index], 2) < cave_size) {
						if (ndef->get(c).is_ground_content || c == CONTENT_AIR) {
							if (c != CONTENT_AIR)
								// within the cave
								vm->m_data[i] = MapNode(CONTENT_AIR);
						} else if (c == c_river_water_source || c == c_lava_source) {
							// Trim out any source that would end up floating
							//  in the middle of a cave. It looks weird.
							u32 il = vm->m_area.index(x, y-1, z);
							content_t cl = vm->m_data[il].getContent();
							if (cl != c_stone)
								vm->m_data[i] = MapNode(CONTENT_AIR);
						}
					}
				}
	}
}


void MapgenValleys::generateCaves(s16 max_stone_y)
{
	if (max_stone_y >= node_min.Y) {
		u32 index   = 0;

		for (s16 z = node_min.Z; z <= node_max.Z; z++)
		for (s16 y = node_min.Y - 1; y <= node_max.Y + 1; y++) {
			u32 i = vm->m_area.index(node_min.X, y, z);
			for (s16 x = node_min.X; x <= node_max.X; x++, i++, index++) {
				float d1 = contour(noise_v7_caves_1->result[index]);
				float d2 = contour(noise_v7_caves_2->result[index]);
				if (d1 * d2 > 0.3) {
					content_t c = vm->m_data[i].getContent();
					if (!ndef->get(c).is_ground_content || c == CONTENT_AIR)
						continue;

					vm->m_data[i] = MapNode(CONTENT_AIR);
				}
			}
		}
	}

	PseudoRandom ps(blockseed + 21343);
	u32 bruises_count = (ps.range(1, 4) == 1) ? ps.range(1, 2) : 0;
	for (u32 i = 0; i < bruises_count; i++) {
		CaveValleys cave(this, &ps);
		cave.makeCave(node_min, node_max, max_stone_y);
	}
}
