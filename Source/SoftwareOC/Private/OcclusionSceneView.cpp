﻿

#include "OcclusionSceneView.h"

#include "SoftwareOCSettings.h"
#include "Runtime/Renderer/Private/ScenePrivate.h"
#include "Runtime/Renderer/Private/SceneRendering.h"
#include "PrimitiveSceneInfo.h"
#include "Unreal/FOcclusionFrameResults.h"
#include "Unreal/SceneSoftwareOcclusion.h"

static FSceneSoftwareOcclusion* SceneSoftwareOcclusion;

FOcclusionSceneViewExtension::FOcclusionSceneViewExtension(const FAutoRegister& AutoRegister)
	: FSceneViewExtensionBase(AutoRegister)
{
	SceneSoftwareOcclusion = new FSceneSoftwareOcclusion{};
}

FOcclusionSceneViewExtension::~FOcclusionSceneViewExtension()
{
	if(SceneSoftwareOcclusion)
	{
		delete SceneSoftwareOcclusion;
	}
}

void FOcclusionSceneViewExtension::PostRenderBasePassDeferred_RenderThread(FRDGBuilder& GraphBuilder,
                                                                           FSceneView& InView, const FRenderTargetBindingSlots& RenderTargets,
                                                                           TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures)
{
	FSceneViewExtensionBase::
		PostRenderBasePassDeferred_RenderThread(GraphBuilder, InView, RenderTargets, SceneTextures);

	FSceneViewExtensionBase::PostRenderView_RenderThread(GraphBuilder, InView);

	if(OcSubsystem)
	{
		SceneSoftwareOcclusion->OcSubsystem = OcSubsystem;
	}
	
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
		
	SceneSoftwareOcclusion->Process(Scene, *View);

	FOcclusionFrameResults* FrameResults = SceneSoftwareOcclusion->Available.Get();

	if (!FrameResults)
	{
		return;
	}
	
	for (TObjectIterator<UStaticMeshComponent> StaticMeshIterator; StaticMeshIterator; ++StaticMeshIterator)
	{
		UStaticMeshComponent* Component = *StaticMeshIterator;
		if(!IsValid(Component))
		{
			continue;
		}

		if(Component->HasAnyFlags(RF_ClassDefaultObject))
		{
			continue;
		}

		Component->SetHiddenInGame(!FrameResults->IsVisible(Component->GetPrimitiveSceneId()));
	}
}
