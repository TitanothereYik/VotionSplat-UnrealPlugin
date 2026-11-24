#include "VotionSplatShaders.h"

IMPLEMENT_GLOBAL_SHADER(FVotionProjectCS, "/Plugin/VotionSplat/Private/VotionSplatProject.usf", "ProjectCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FVotionSortKeysCS, "/Plugin/VotionSplat/Private/VotionSplatSortKeys.usf", "KeysCS", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FVotionRasterVS, "/Plugin/VotionSplat/Private/VotionSplatRaster.usf", "MainVS", SF_Vertex);
IMPLEMENT_GLOBAL_SHADER(FVotionRasterPS, "/Plugin/VotionSplat/Private/VotionSplatRasterPS.usf", "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FVotionCompositePS, "/Plugin/VotionSplat/Private/VotionSplatComposite.usf", "MainPS", SF_Pixel);
