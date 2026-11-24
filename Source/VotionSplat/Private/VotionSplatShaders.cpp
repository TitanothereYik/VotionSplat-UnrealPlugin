#include "VotionSplatShaders.h"

IMPLEMENT_GLOBAL_SHADER(FVotionProjectCS, "/Plugin/VotionSplat/Private/VotionSplatProject.usf", "ProjectCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FVotionSortKeysCS, "/Plugin/VotionSplat/Private/VotionSplatSortKeys.usf", "KeysCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FVotionRasterVS, "/Plugin/VotionSplat/Private/VotionSplatRaster.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FVotionRasterPS, "/Plugin/VotionSplat/Private/VotionSplatRasterPS.usf", "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FVotionCompositePS, "/Plugin/VotionSplat/Private/VotionSplatComposite.usf", "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FVotionCompactCountCS, "/Plugin/VotionSplat/Private/VotionSplatCompact.usf", "CountCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FVotionCompactScanCS, "/Plugin/VotionSplat/Private/VotionSplatCompact.usf", "ScanCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FVotionCompactWriteCS, "/Plugin/VotionSplat/Private/VotionSplatCompact.usf", "WriteCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FVotionScanReduceCS, "/Plugin/VotionSplat/Private/VotionSplatScan.usf", "ReduceCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FVotionScanGroupsCS, "/Plugin/VotionSplat/Private/VotionSplatScan.usf", "GroupsCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FVotionScanWriteCS, "/Plugin/VotionSplat/Private/VotionSplatScan.usf", "WriteCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FVotionRadixHistCS, "/Plugin/VotionSplat/Private/VotionSplatRadix.usf", "HistCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FVotionRadixScatterCS, "/Plugin/VotionSplat/Private/VotionSplatRadix.usf", "ScatterCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FVotionTileCountCS, "/Plugin/VotionSplat/Private/VotionSplatTile.usf", "CountCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FVotionTileEmitCS, "/Plugin/VotionSplat/Private/VotionSplatTile.usf", "EmitCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FVotionTileRangesCS, "/Plugin/VotionSplat/Private/VotionSplatTile.usf", "RangesCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FVotionTileFinalizeCS, "/Plugin/VotionSplat/Private/VotionSplatTile.usf", "FinalizeCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FVotionTileRasterCS, "/Plugin/VotionSplat/Private/VotionSplatTile.usf", "RasterCS", SF_Compute);
