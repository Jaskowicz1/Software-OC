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

			IndicesSP.SetNumUninitialized(NumIndices);
			
			const FVector3f* V0 = &LODModel.VertexBuffers.PositionVertexBuffer.VertexPosition(0);
			const uint16* Indices = IndexBuffer.AccessStream16();

			if (V0)
			{
				FMemory::Memcpy(VerticesSP.GetData(), V0, NumVtx * sizeof(FVector3f));
			}

			if (Indices)
			{
				FMemory::Memcpy(IndicesSP.GetData(), Indices, NumIndices * sizeof(uint16));
			}
		}
	}

	UPROPERTY(Transient)
	FMatrix					LocalToWorld{};
	UPROPERTY(Transient)
	TArray<FVector3f>		VerticesSP{};
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