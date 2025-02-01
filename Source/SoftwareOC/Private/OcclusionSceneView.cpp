

#include "OcclusionSceneView.h"

#include "Runtime/Renderer/Private/ScenePrivate.h"
#include "Runtime/Renderer/Private/SceneRendering.h"
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
		SceneSoftwareOcclusion->OcSubsystem = nullptr;
		delete SceneSoftwareOcclusion;
	}
}

void FOcclusionSceneViewExtension::PostRenderBasePassDeferred_RenderThread(FRDGBuilder& GraphBuilder,
                                                                           FSceneView& InView, const FRenderTargetBindingSlots& RenderTargets,
                                                                           TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures)
{
	FSceneViewExtensionBase::
		PostRenderBasePassDeferred_RenderThread(GraphBuilder, InView, RenderTargets, SceneTextures);

	if(OcSubsystem)
	{
		SceneSoftwareOcclusion->OcSubsystem = OcSubsystem;
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
	// This function shouldn't call if we are frozen, but it looks like Unreal is still doing stuff in the back when this isn't here?
	// No breakpoints get called though...
	if (!ViewState || ViewState->bIsFrozen || ViewState->bIsFreezing)
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
		if (!IsValid(Component) || !Component->IsRegistered() || !Component->GetWorld() ||
			Component->GetWorld()->WorldType == EWorldType::Editor || Component->GetWorld()->WorldType == EWorldType::Inactive ||
			Component->GetWorld()->WorldType == EWorldType::Inactive)
		{
			continue;
		}

		if(Component->HasAnyFlags(RF_ClassDefaultObject))
		{
			continue;
		}

		bool ObjectVisiblity = !FrameResults->IsVisible(Component->GetPrimitiveSceneId());;

		AsyncTask(ENamedThreads::GameThread, [Component, ObjectVisiblity]()
		{
			if(IsValid(Component) && Component->GetWorld())
			{
				Component->SetHiddenInGame(ObjectVisiblity);
			}
		});
	}
}

void FOcclusionSceneViewExtension::PostRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView)
{
	FSceneViewExtensionBase::PostRenderView_RenderThread(GraphBuilder, InView);
	
	if (!InView.bIsViewInfo)
	{
		return;
	}

	FViewInfo* View = (FViewInfo*)(&InView);
	
	if (!View || !InView.ViewActor || !InView.ViewActor->GetWorld() || !InView.ViewActor->GetWorld()->Scene)
	{
		return;
	}

	FRDGTextureRef ViewFamilyTexture = TryCreateViewFamilyTexture(GraphBuilder, *InView.Family);
	
	const FScreenPassRenderTarget Output(ViewFamilyTexture, View->UnconstrainedViewRect, ERenderTargetLoadAction::ELoad);
	
	SceneSoftwareOcclusion->DebugDraw(GraphBuilder, *View, Output, 20, 20);
}
