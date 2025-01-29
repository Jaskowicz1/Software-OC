#pragma once

static constexpr int32 BIN_WIDTH = 64;
static constexpr int32 BIN_NUM = 6;
static constexpr int32 FRAMEBUFFER_WIDTH = BIN_WIDTH*BIN_NUM;
static constexpr int32 FRAMEBUFFER_HEIGHT = 256;

struct FFramebufferBin
{
	uint64 Data[FRAMEBUFFER_HEIGHT];
};

struct FOcclusionFrameResults
{
	FFramebufferBin	Bins[BIN_NUM];
	TMap<FPrimitiveComponentId, bool> VisibilityMap;

	inline bool IsVisible(FPrimitiveComponentId ID)
	{
		if(!VisibilityMap.Contains(ID))
		{
			return true; // Assume that any mesh that isn't in this list, should be visible.
		}
		
		return *VisibilityMap.Find(ID);
	}
};
