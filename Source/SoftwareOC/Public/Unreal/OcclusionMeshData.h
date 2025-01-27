#pragma once

#include "OcclusionMeshData.generated.h"

USTRUCT()
struct FOcclusionMeshData
{
	GENERATED_BODY()
	
public:

	FOcclusionMeshData() = default;

	FOcclusionMeshData(const UStaticMesh* Mesh)
	{
		if(!IsValid(Mesh))
		{
			return;
		}
		
		// Archie - This TO-DO is left in by unreal, though, it still is a thing to maybe implement.
		// TODO: Custom geometry for occluder mesh?
		//int32 LODIndex = FMath::Min(Owner->LODForOccluderMesh, Owner->GetRenderData()->LODResources.Num()-

		const FStaticMeshLODResources& LODModel = Mesh->GetRenderData()->LODResources[Mesh->GetRenderData()->CurrentFirstLODIdx];
			
		const FRawStaticIndexBuffer& IndexBuffer = LODModel.DepthOnlyIndexBuffer.GetNumIndices() > 0 ? LODModel.DepthOnlyIndexBuffer : LODModel.IndexBuffer;
		int32 NumVtx = LODModel.VertexBuffers.PositionVertexBuffer.GetNumVertices();
		int32 NumIndices = IndexBuffer.GetNumIndices();
		
		if (NumVtx > 0 && NumIndices > 0 && !IndexBuffer.Is32Bit())
		{
			VerticesSP.SetNumUninitialized(NumVtx);

			// Do this instead of memcpy because memcpy is stupid apparently and just dies.
			for (int i = 0; i < NumVtx; ++i)
			{
				VerticesSP.GetData()[i] = FVector(LODModel.VertexBuffers.PositionVertexBuffer.VertexPosition(i));
			}

			IndicesSP.SetNumUninitialized(NumIndices);
			for (int i = 0; i < NumIndices; ++i)
			{
				IndicesSP.GetData()[i] = IndexBuffer.AccessStream16()[i];
			}
		}
	}

	UPROPERTY(Transient)
	FMatrix					LocalToWorld{};
	UPROPERTY(Transient)
	TArray<FVector>			VerticesSP{};
	UPROPERTY(Transient)
	TArray<uint16>			IndicesSP{};
	
	FPrimitiveComponentId	PrimId; // Has a zero constructor.
};

struct FPotentialOccluderPrimitive
{
	const FPrimitiveSceneInfo* PrimitiveSceneInfo = nullptr;
	FOcclusionMeshData OccluderData{}; // Because unreal removed this from individual proxies. ugh.
	float Weight{ 0.f };
};