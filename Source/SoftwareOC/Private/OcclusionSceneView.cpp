

#include "OcclusionSceneView.h"

#include "Runtime/Renderer/Private/ScenePrivate.h"
#include "Runtime/Renderer/Private/SceneRendering.h"
#include "Unreal/FOcclusionFrameResults.h"
#include "Unreal/SceneSoftwareOcclusion.h"

static FSceneSoftwareOcclusion* SceneSoftwareOcclusion;

DECLARE_STATS_GROUP(TEXT("Software Occlusion"), STATGROUP_SoftwareOcclusion, STATCAT_Advanced);
DECLARE_DWORD_COUNTER_STAT(TEXT("SceneView Culled"),STAT_SoftwareCulledSceneView,STATGROUP_SoftwareOcclusion);

static int32 GSOVisualizeOccluded = 0;
static FAutoConsoleVariableRef CVarSOVisualizeOccluded(
	TEXT("r.so.VisualizeOccluded"),
	GSOVisualizeOccluded,
	TEXT("Visualize Occluded and Non-Occluded Objects."),
	ECVF_RenderThreadSafe
	);

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
		
	SceneSoftwareOcclusion->Process(Scene, *View);

	FOcclusionFrameResults* FrameResults = SceneSoftwareOcclusion->Available.Get();

	if (!FrameResults)
	{
		return;
	}

	int NumOccluded = 0;
	
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

		const bool ObjectHidden = !FrameResults->IsVisible(Component->GetPrimitiveSceneId());

		AsyncTask(ENamedThreads::GameThread, [Component, ObjectHidden]()
		{
			if(IsValid(Component) && Component->GetWorld())
			{
				Component->SetHiddenInGame(ObjectHidden);
			}
		});

		if(GSOVisualizeOccluded)
		{
			FColor OccludedColour = ObjectHidden ? FColor::Green : FColor::Red;

			if(Component->GetWorld())
			{
				DrawDebugBox(Component->GetWorld(), Component->Bounds.Origin, Component->Bounds.BoxExtent, FQuat::Identity, OccludedColour, false, -1, 10, 1);	
			}
		}
		
		if(ObjectHidden)
		{
			NumOccluded++;
		}
	}
	
	INC_DWORD_STAT_BY(STAT_SoftwareCulledSceneView, NumOccluded);

	FRDGTextureRef ViewFamilyTexture = TryCreateViewFamilyTexture(GraphBuilder, *InView.Family);
	
	const FScreenPassRenderTarget Output(ViewFamilyTexture, View->UnconstrainedViewRect, ERenderTargetLoadAction::ELoad);
	
	SceneSoftwareOcclusion->DebugDraw(GraphBuilder, *View, Output, 20, 20);
}
