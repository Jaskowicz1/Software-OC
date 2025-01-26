// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "OcclusionMeshData.h"
#include "Async/TaskGraphInterfaces.h"

class FRHICommandListImmediate;
class FScene;
class FViewInfo;
struct FOcclusionFrameResults;

class FOccluderElementsCollector
{
public:
	virtual ~FOccluderElementsCollector() {};
	virtual void AddElements(const FOccluderVertexArraySP& Vertices, const FOccluderIndexArraySP& Indices, const FMatrix& LocalToWorld)
	{}
};

class FSceneSoftwareOcclusion
{
public:
	FSceneSoftwareOcclusion();
	~FSceneSoftwareOcclusion();

	int32 Process(const FScene* Scene, FViewInfo& View);
	void FlushResults();
	void DebugDraw(FCanvas* Canvas, int32 InX, int32 InY) const;
	static int32 CollectOccluderElements(FOccluderElementsCollector& Collector, FPrimitiveSceneProxy* Proxy, const FPotentialOccluderPrimitive& PotentialOccluder);
	
private:
	FGraphEventRef TaskRef;
	TUniquePtr<FOcclusionFrameResults> Available;
	TUniquePtr<FOcclusionFrameResults> Processing;
};