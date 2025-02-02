

#include "OcclusionSceneView.h"

#include "SoftwareOCSubsystem.h"
#include "Camera/CameraComponent.h"
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
	OcSubsystem->CachedVisibilityMap.Empty();
	
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

	FViewInfo* View = static_cast<FViewInfo*>(&InView);
	if (!View || !InView.ViewActor || !InView.ViewActor->GetWorld() || !InView.ViewActor->GetWorld()->Scene)
	{
		return;
	}

	const FSceneViewState* ViewState = static_cast<FSceneViewState*>(View->State);
	if (!ViewState || ViewState->bIsFrozen || ViewState->bIsFreezing)
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

	for (auto& Tuple : FrameResults->VisibilityMap)
	{
		if (Tuple.Value)
		{
			OcSubsystem->CachedVisibilityMap.Remove(Tuple.Key); // Removing values ensures that the Gather step doesn't spend too long checked already cached values.
		}
		else
		{
			OcSubsystem->CachedVisibilityMap.Add(Tuple.Key, Tuple.Value);
		}
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

		// Ignore this type of object just to speed up the looping (this is more for multiplayer games where you can get
		// several cameras. Allowing us to loop through fast.
		if(Component->IsA<UCameraProxyMeshComponent>())
		{
			continue;
		}

		// Now make sure that these components aren't marked to be ignored.
		// If they are, don't bother with them (saves us time).
		if(Component->bTreatAsBackgroundForOcclusion == 1 || (Component->SceneProxy && !Component->SceneProxy->CanBeOccluded()))
		{
			continue;
		}

		FPrimitiveComponentId ComponentId = Component->GetPrimitiveSceneId();

		if(Component->HasAnyFlags(RF_ClassDefaultObject))
		{
			continue;
		}

		bool ObjectHidden = !FrameResults->IsVisible(ComponentId);
		
		// Ensure that any values that aren't in for this frame, are checked against a cache.
		if(OcSubsystem->CachedVisibilityMap.Contains(Component->GetPrimitiveSceneId()))
		{
			ObjectHidden = !(*OcSubsystem->CachedVisibilityMap.Find(ComponentId));
		}

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

			if(!FrameResults->VisibilityMap.Contains(ComponentId))
			{
				if(!OcSubsystem->CachedVisibilityMap.Contains(ComponentId))
				{
					OccludedColour = FColor::Yellow;
				}
				else
				{
					OccludedColour = ObjectHidden ? FColor::Magenta : FColor::Red;
				}
			}

			// DrawDebugBox throws an error if we try draw immediately. We should until the world has been alive
			// for a bit to actually draw debug stuff.
			if(Component->GetWorld() && Component->GetWorld()->GetRealTimeSeconds() >= 1)
			{
				if(GEngine)
				{
					DrawDebugBox(Component->GetWorld(), Component->Bounds.Origin, Component->Bounds.BoxExtent, FQuat::Identity, OccludedColour, false, -1, 10, 2);
				}
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
