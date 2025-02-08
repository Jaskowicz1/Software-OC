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

// Needed to prevent "abstract class" errors.
TStatId USoftwareOCSubsystem::GetStatId() const
{
	return UObject::GetStatID();
}

bool USoftwareOCSubsystem::IsTickable() const
{
	const USoftwareOCSettings* OcclusionSettings = GetDefault<USoftwareOCSettings>();

	return OcclusionSettings->bEnableOcclusion;
}

void USoftwareOCSubsystem::Tick(float DeltaTime)
{
	ForceUpdateMap();
}

void USoftwareOCSubsystem::ForceUpdateMap()
{
	for(TObjectIterator<UStaticMeshComponent> StaticMeshItr; StaticMeshItr; ++StaticMeshItr)
	{
		UStaticMeshComponent* Component = *StaticMeshItr;
		if (!IsValid(Component) || !Component->IsRegistered() || !Component->GetWorld() ||
			Component->GetWorld()->WorldType == EWorldType::Editor || Component->GetWorld()->WorldType == EWorldType::Inactive ||
			Component->GetWorld()->WorldType == EWorldType::Inactive)
		{
			continue;
		}

		if (Component->HasAnyFlags(RF_ClassDefaultObject))
		{
			continue;
		}
		
		IDToMeshComp.Add(Component->GetPrimitiveSceneId().PrimIDValue, Component);
	}
}
