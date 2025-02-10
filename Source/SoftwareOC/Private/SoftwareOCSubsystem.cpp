// Copyright - Archie Jaskowicz

#include "SoftwareOCSubsystem.h"

#include "SceneViewExtension.h"
#include "SoftwareOCSettings.h"

void USoftwareOCSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const USoftwareOCSettings* OcclusionSettings = GetDefault<USoftwareOCSettings>();

	if (OcclusionSettings->bEnableOcclusion)
	{
		OcclusionSceneViewExtension = FSceneViewExtensions::NewExtension<FOcclusionSceneViewExtension>();
		OcclusionSceneViewExtension->OcSubsystem = this;
	}
#if defined(ENGINE_MINOR_VERSION) && ENGINE_MINOR_VERSION >= 5
	else // If UE version is 5.5, outright disable tick (this doesn't exist for ue5.4 and lower.
	{
		SetTickableTickType(ETickableTickType::Never); // Do not allow ticking if we aren't occluding.
	}
#endif
}

void USoftwareOCSubsystem::Deinitialize()
{
	Super::Deinitialize();

	IDToMeshComp = {};

	if(OcclusionSceneViewExtension)
	{
		OcclusionSceneViewExtension.Reset();
	}
}

TStatId USoftwareOCSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(USoftwareOCSubsystem, STATGROUP_Tickables);
}

bool USoftwareOCSubsystem::IsTickable() const
{
	// We only want to do the settings check in UE5.4 and lower. UE5.5 will already abort tick in Initialise.
	// This is a very minor optimisation, but makes no sense to ignore.
#if defined(ENGINE_MINOR_VERSION) && ENGINE_MINOR_VERSION < 5
	const USoftwareOCSettings* OcclusionSettings = GetDefault<USoftwareOCSettings>();

	return OcclusionSettings->bEnableOcclusion;
#else
	return FTickableGameObject::IsTickable();
#endif
}

void USoftwareOCSubsystem::Tick(float DeltaTime)
{
	ForceUpdateMap();
}

void USoftwareOCSubsystem::ForceUpdateMap()
{
	// Empty list just incase it's got dangling pointers (Shouldn't but never worth the risk).
	IDToMeshComp.Empty();
	
	for(TObjectIterator<UMeshComponent> MeshItr; MeshItr; ++MeshItr)
	{
		UMeshComponent* Component = *MeshItr;
		if (!IsValid(Component) || !Component->IsRegistered() || !Component->GetWorld() ||
			Component->GetWorld()->WorldType == EWorldType::Editor || Component->GetWorld()->WorldType == EWorldType::Inactive ||
			Component->GetWorld()->WorldType == EWorldType::Inactive || Component->GetWorld() != GetWorld())
		{
			continue;
		}

		if (Component->HasAnyFlags(RF_ClassDefaultObject))
		{
			continue;
		}
		
		IDToMeshComp.Add(Component->GetPrimitiveSceneId().PrimIDValue, Component);
	}

	// Clean up Cache maps, as they may not be valid and won't auto update (not marked as UPROPERTY and can't be).
	
	TArray<FPrimitiveComponentId> IDsToRemove;

	for(auto& Tuple : CachedVisibilityMap)
	{
		if(!IDToMeshComp.Contains(Tuple.Key.PrimIDValue))
		{
			IDsToRemove.Add(Tuple.Key);
		}
	}

	for(FPrimitiveComponentId ID : IDsToRemove)
	{
		CachedVisibilityMap.Remove(ID);
		CachedHiddenMap.Remove(ID);
	}
}
