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

#ifndef MAPGEN_VALLEYS_HEADER
#define MAPGEN_VALLEYS_HEADER

#include "mapgen.h"

/////////////////// Mapgen Valleys flags
#define MG_VALLEYS_V7_CAVES 0x01
#define MG_VALLEYS_LAVA 0x02
#define MG_VALLEYS_GROUND_WATER 0x04
#define MG_VALLEYS_PROFILE 0x08

class BiomeManager;

extern FlagDesc flagdesc_mapgen_valleys[];


struct MapgenValleysParams : public MapgenSpecificParams {
	u32 spflags;

	s16 river_size;
	s16 river_depth;
	s16 water_level;
	s16 altitude_chill;
	s16 cave_size;
	s16 humidity;
	s16 temperature;
	s16 lava_max_height;
	s16 lava_frequency;
	s16 ground_water_frequency;

	NoiseParams np_filler_depth;
	NoiseParams np_biome_heat;
	NoiseParams np_biome_heat_blend;
	NoiseParams np_biome_humidity;
	NoiseParams np_biome_humidity_blend;

	// vmg noises
	NoiseParams np_terrain_height;
	NoiseParams np_rivers;
	NoiseParams np_valley_depth;
	NoiseParams np_valley_profile;
	NoiseParams np_inter_valley_slope;
	NoiseParams np_inter_valley_fill;
	NoiseParams np_v7_caves_1;
	NoiseParams np_v7_caves_2;
	NoiseParams np_caves_1;
	NoiseParams np_caves_2;
	NoiseParams np_caves_3;
	NoiseParams np_caves_4;
	NoiseParams np_stala;
	NoiseParams np_lava_1;
	NoiseParams np_lava_2;
	NoiseParams np_water_1;
	NoiseParams np_water_2;

	MapgenValleysParams();
	~MapgenValleysParams() {}

	void readParams(const Settings *settings);
	void writeParams(Settings *settings) const;
};

class MapgenValleys : public Mapgen {
public:
	EmergeManager *m_emerge;
	BiomeManager *bmgr;

	int ystride;
	int zstride;
	u32 spflags;

	v3s16 node_min;
	v3s16 node_max;
	v3s16 full_node_min;
	v3s16 full_node_max;

	s16 *ridge_heightmap;

	Noise *noise_filler_depth;

	Noise *noise_heat;
	Noise *noise_humidity;
	Noise *noise_heat_blend;
	Noise *noise_humidity_blend;

	// vmg noises
	Noise *noise_terrain_height;
	Noise *noise_rivers;
	Noise *noise_valley_depth;
	Noise *noise_valley_profile;
	Noise *noise_inter_valley_slope;
	Noise *noise_inter_valley_fill;
	Noise *noise_v7_caves_1;
	Noise *noise_v7_caves_2;
	Noise *noise_caves_1;
	Noise *noise_caves_2;
	Noise *noise_caves_3;
	Noise *noise_caves_4;
	Noise *noise_stala;
	Noise *noise_lava_1;
	Noise *noise_lava_2;
	Noise *noise_water_1;
	Noise *noise_water_2;

	float altitude_chill;
	float river_size;
	float cave_size;
	float river_depth;
	float water_level;
	float humidity_adjust;
	float temperature_adjust;
	float lava_max_height;
	float lava_frequency;
	float ground_water_frequency;

	content_t c_stone;
	content_t c_water_source;
	content_t c_river_water_source;
	content_t c_sand;
	content_t c_lava_source;
	content_t c_desert_stone;
	content_t c_ice;
	content_t c_sandstone;

	content_t c_cobble;
	content_t c_stair_cobble;
	content_t c_mossycobble;
	content_t c_sandstonebrick;
	content_t c_stair_sandstonebrick;

	MapgenValleys(int mapgenid, MapgenParams *params, EmergeManager *emerge);
	~MapgenValleys();

	virtual void makeChunk(BlockMakeData *data);
	Biome *getBiomeAtPoint(v3s16 p);

	int getGroundLevelAtPoint(v2s16 p);
	float baseTerrainLevelAtPoint(s16 x, s16 z);
	float baseTerrainLevelFromMap(int index, float *river_y);

	void calcBiomes(s16 sx, s16 sy, float *heat_map, float *humidity_map, s16 *height_map, u8 *biomeid_map);

	void calculateNoise();

	virtual int generateTerrain();
	float baseGroundFromNoise(s16 x, s16 z, float valley_depth, float terrain_height, float *rivers, float valley_profile, float inter_valley_slope, float *valley, float inter_valley_fill);
	float humidityByTerrain(float humidity, float mount, float valley);

	MgStoneType generateBiomes(float *heat_map, float *humidity_map);
	void fixRivers(s16 sx, s16 sy, s16 *height_map);
	void dustTopNodes();

	//void addTopNodes();

	void generateCaves(s16 max_stone_y);
	void generateVmgCaves(s16 max_stone_y);
};

struct MapgenFactoryValleys : public MapgenFactory {
	Mapgen *createMapgen(int mgid, MapgenParams *params, EmergeManager *emerge)
	{
		return new MapgenValleys(mgid, params, emerge);
	};

	MapgenSpecificParams *createMapgenParams()
	{
		return new MapgenValleysParams();
	};
};

#endif
