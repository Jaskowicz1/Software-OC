#pragma once

#include "OcclusionMeshData.generated.h"

typedef TArray<FVector> FOccluderVertexArray;
typedef TArray<uint16> FOccluderIndexArray;
typedef TSharedPtr<FOccluderVertexArray, ESPMode::ThreadSafe> FOccluderVertexArraySP;
typedef TSharedPtr<FOccluderIndexArray, ESPMode::ThreadSafe> FOccluderIndexArraySP;

USTRUCT()
struct FOcclusionMeshData
{
	GENERATED_BODY()
	
public:

	FOcclusionMeshData() = default;

	FOcclusionMeshData(const UStaticMesh* Mesh)
	{
		// Archie - This TO-DO is left in by unreal, though, it still is a thing to maybe implement.
		// TODO: Custom geometry for occluder mesh?
		//int32 LODIndex = FMath::Min(Owner->LODForOccluderMesh, Owner->GetRenderData()->LODResources.Num()-

		const FStaticMeshLODResources& LODModel = Mesh->GetRenderData()->LODResources[Mesh->GetRenderData()->CurrentFirstLODIdx];
			
		const FRawStaticIndexBuffer& IndexBuffer = LODModel.DepthOnlyIndexBuffer.GetNumIndices() > 0 ? LODModel.DepthOnlyIndexBuffer : LODModel.IndexBuffer;
		int32 NumVtx = LODModel.VertexBuffers.PositionVertexBuffer.GetNumVertices();
		int32 NumIndices = IndexBuffer.GetNumIndices();
		
		if (NumVtx > 0 && NumIndices > 0 && !IndexBuffer.Is32Bit())
		{
			VerticesSP->SetNumUninitialized(NumVtx);

			const FVector3f* V0 = &LODModel.VertexBuffers.PositionVertexBuffer.VertexPosition(0);

			IndicesSP->SetNumUninitialized(NumIndices);
			const uint16* Indices = IndexBuffer.AccessStream16();

			FMemory::Memcpy(VerticesSP->GetData(), V0, NumVtx*sizeof(FVector));
			FMemory::Memcpy(IndicesSP->GetData(), Indices, NumIndices*sizeof(uint16));
		}
	}

	FMatrix					LocalToWorld;
	FOccluderVertexArraySP	VerticesSP;
	FOccluderIndexArraySP   IndicesSP;
	FPrimitiveComponentId	PrimId;
};

struct FPotentialOccluderPrimitive
{
	const FPrimitiveSceneInfo* PrimitiveSceneInfo;
	FOcclusionMeshData OccluderData; // Because unreal removed this from individual proxies. ugh.
	float Weight;
};