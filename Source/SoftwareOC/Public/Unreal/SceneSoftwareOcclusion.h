// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "OcclusionMeshData.h"
#include "ScreenPass.h"
#include "Async/TaskGraphInterfaces.h"

class USoftwareOCSubsystem;
class FRHICommandListImmediate;
class FScene;
class FViewInfo;
struct FOcclusionFrameResults;

class FOccluderElementsCollector
{
public:
	virtual ~FOccluderElementsCollector() {};
	virtual void AddElements(const TArray<FVector>& Vertices, const TArray<uint16>& Indices, const FMatrix& LocalToWorld)
	{}
};

class FSceneSoftwareOcclusion
{
public:
	FSceneSoftwareOcclusion();
	~FSceneSoftwareOcclusion();

	FGraphEventRef SubmitScene(const FScene* Scene, const FViewInfo& View, FOcclusionFrameResults* Results);
	int32 Process(const FScene* Scene, FViewInfo& View);
	void FlushResults();
	void DebugDraw(FRDGBuilder& GraphBuilder, const FViewInfo& View, FScreenPassRenderTarget Output, int32 InX, int32 InY) const;
	static int32 CollectOccluderElements(FOccluderElementsCollector& Collector, FPrimitiveSceneProxy* Proxy, const FPotentialOccluderPrimitive& PotentialOccluder);

	TUniquePtr<FOcclusionFrameResults> Available;
	
	USoftwareOCSubsystem* OcSubsystem;
	
private:
	FGraphEventRef TaskRef;
	TUniquePtr<FOcclusionFrameResults> Processing;
};