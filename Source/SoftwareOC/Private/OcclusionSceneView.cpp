

#include "OcclusionSceneView.h"

#include "SoftwareOCSettings.h"
#include "Runtime/Renderer/Private/ScenePrivate.h"
#include "Runtime/Renderer/Private/SceneRendering.h"
#include "PrimitiveSceneInfo.h"
#include "Unreal/SceneSoftwareOcclusion.h"

static FSceneSoftwareOcclusion* SceneSoftwareOcclusion;

FOcclusionSceneViewExtension::FOcclusionSceneViewExtension(const FAutoRegister& AutoRegister)
	: FSceneViewExtensionBase(AutoRegister)
{
	SceneSoftwareOcclusion = new FSceneSoftwareOcclusion{};
}

void FOcclusionSceneViewExtension::PostRenderBasePassDeferred_RenderThread(FRDGBuilder& GraphBuilder,
	FSceneView& InView, const FRenderTargetBindingSlots& RenderTargets,
	TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures)
{
	FSceneViewExtensionBase::
		PostRenderBasePassDeferred_RenderThread(GraphBuilder, InView, RenderTargets, SceneTextures);

	FSceneViewExtensionBase::PostRenderView_RenderThread(GraphBuilder, InView);
	
	const USoftwareOCSettings* OcclusionSettings = GetDefault<USoftwareOCSettings>();

	if (!OcclusionSettings->bEnableOcclusion)
	{
		return;
	}

	if (!InView.bIsViewInfo)
	{
		return;
	}

	FViewInfo* View = (FViewInfo*)(&InView);
	
	if (!View || !InView.ViewActor || !InView.ViewActor->GetWorld() || !InView.ViewActor->GetWorld()->Scene)
	{
		return;
	}

	FScene* Scene = InView.ViewActor->GetWorld()->Scene->GetRenderScene();
	
	if (!Scene)
	{
		return;
	}
	
	FSceneViewState* ViewState = (FSceneViewState*)View->State;

	if (!ViewState || ViewState->bIsFrozen)
	{
		return;
	}
		
	SceneSoftwareOcclusion->Process(Scene, *View);
	
	TArray<FPrimitiveSceneInfo*> AddedSceneInfos;
	for (TConstDualSetBitIterator<SceneRenderingBitArrayAllocator, FDefaultBitArrayAllocator> BitIt(View->PrimitiveVisibilityMap, Scene->PrimitivesNeedingStaticMeshUpdate); BitIt; ++BitIt)
	{
		int32 PrimitiveIndex = BitIt.GetIndex();
		AddedSceneInfos.Add(Scene->Primitives[PrimitiveIndex]);
	}

	if (AddedSceneInfos.Num() > 0)
	{
		FPrimitiveSceneInfo::UpdateStaticMeshes(Scene, AddedSceneInfos, EUpdateStaticMeshFlags::AllCommands);
	}
}
