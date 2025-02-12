// Copyright - Archie Jaskowicz.

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
	if(SceneSoftwareOcclusion)
	{
		SceneSoftwareOcclusion->OcSubsystem = nullptr;
		delete SceneSoftwareOcclusion;
	}
	
	OcSubsystem->IDToMeshComp.Empty();
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
	} else
	{
		return;
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
	
	if(!Scene->World || !IsValid(Scene->World) || Scene->World->IsBeingCleanedUp() || Scene->World->HasAnyFlags(RF_MirroredGarbage) || Scene->World->HasAnyFlags(RF_BeginDestroyed))
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

	for (TObjectIterator<UMeshComponent> MeshIterator; MeshIterator; ++MeshIterator)
	{
		UMeshComponent* Component = *MeshIterator;
		if (!IsValid(Component) || !Component->IsRegistered() || !Component->GetWorld() ||
			Component->GetWorld()->WorldType == EWorldType::Editor ||
			Component->GetWorld()->WorldType == EWorldType::Inactive ||
			Component->GetWorld() != OcSubsystem->GetWorld())
		{
			continue;
		}

		// Paranoid sanity checks.
		if(Component->GetWorld()->IsBeingCleanedUp() || Component->GetWorld()->HasAnyFlags(RF_MirroredGarbage) ||
			Component->GetWorld()->HasAnyFlags(RF_BeginDestroyed) || !Component->GetWorld()->HasAnyFlags(RF_WasLoaded))
		{
			return;
		}

		// Now make sure that these components aren't marked to be ignored.
		// If they are, don't bother with them (saves us time).
		if(Component->bTreatAsBackgroundForOcclusion == 1 || (Component->SceneProxy && !Component->SceneProxy->CanBeOccluded()))
		{
			continue;
		}

		FPrimitiveComponentId ComponentId = Component->GetPrimitiveSceneId();

		if(Component->HasAnyFlags(RF_ClassDefaultObject) || Component->HasAnyFlags(RF_MirroredGarbage) || Component->HasAnyFlags(RF_BeginDestroyed))
		{
			continue;
		}

		bool ObjectHidden = !FrameResults->IsVisible(ComponentId);

		// This prevents us from making meshes visible that weren't in the visibility map
		// as IsVisible will just return true if a ComponentID wasn't found in the visibility map.
		if(!FrameResults->VisibilityMap.Contains(ComponentId))
		{
			ObjectHidden = Component->bHiddenInGame;
		}
		
		if(!ObjectHidden)
		{
			// This is to fix animations not playing in UE5.5 when occlusion culling is off.
			if(Component->GetWorld())
			{
				Component->GetSceneData().SetLastRenderTime(Component->GetWorld()->GetTimeSeconds(), true);
			}
		}

		// We shouldn't tell the GameThead to launch a task to set visibility if we haven't changed.
		// This can result in a major optimisation if there are thousands of objects in a scene.
		if(Component->bHiddenInGame != ObjectHidden)
		{
			AsyncTask(ENamedThreads::GameThread, [this, Scene, Component, ObjectHidden]()
			{
				// Objects can start to die in this frame (since it runs 1 frame later than the code outside of this task).
				// We detect them here and stop processing them if so.
				if(!Component || !Component->IsRegistered() || Component->HasAnyFlags(RF_MirroredGarbage) ||
					Component->HasAnyFlags(RF_BeginDestroyed) ||
					!Component->GetWorld()->HasAnyFlags(RF_WasLoaded) ||
					Component->IsBeingDestroyed())
				{
					return;
				}

				// Paranoid Sanity Check.
				if(!Scene->World || !IsValid(Scene->World) || Scene->World->IsBeingCleanedUp() || Scene->World->HasAnyFlags(RF_MirroredGarbage) || Scene->World->HasAnyFlags(RF_BeginDestroyed))
				{
					return;
				}
				
				if(!IsValid(Component) || !Component->GetWorld())
				{
					return;
				}

				Component->SetHiddenInGame(ObjectHidden);
			});
		}

		if(GSOVisualizeOccluded)
		{
			FColor OccludedColour = ObjectHidden ? FColor::Green : FColor::Red;

			if(!FrameResults->VisibilityMap.Contains(ComponentId))
			{
				OccludedColour = FColor::Yellow;
			}

			// DrawDebugBox throws an error if we try draw immediately. We should until the world has been alive
			// for a bit to actually draw debug stuff.
			if(Component->GetWorld() && Component->GetWorld()->GetRealTimeSeconds() >= 1)
			{
				if(GEngine)
				{
					// If there are too many objects (or we desync too much), we'll end up throwing an asset when drawing a debug box.
					// So, we offload to GameThread to let it handle drawing.

					// TODO: Fix the jitter as moving to GameThread now makes it desynced.
					AsyncTask(ENamedThreads::GameThread, [Component, OccludedColour]()
					{
						if(IsValid(Component) && Component->GetWorld())
						{
							DrawDebugBox(Component->GetWorld(), Component->Bounds.Origin, Component->Bounds.BoxExtent, FQuat::Identity, OccludedColour, false, -1, 10, 2);
						}
					});
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
