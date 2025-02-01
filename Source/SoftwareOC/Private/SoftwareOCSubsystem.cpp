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
	} else
	{
		SetTickableTickType(ETickableTickType::Never); // Do not allow ticking if we aren't occluding.
	}
}

void USoftwareOCSubsystem::Deinitialize()
{
	Super::Deinitialize();

	IDToMeshComp.Reset();
	IDToMeshComp = {};

	if(OcclusionSceneViewExtension)
	{
		OcclusionSceneViewExtension.Reset();
	}
}

TStatId USoftwareOCSubsystem::GetStatId() const
{
	return UObject::GetStatID();
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
		if (!IsValid(Component) || !Component->GetStaticMesh() || !Component->GetWorld())
		{
			continue;
		}
		
		IDToMeshComp.Add(Component->GetPrimitiveSceneId().PrimIDValue, Component);
	}
}
