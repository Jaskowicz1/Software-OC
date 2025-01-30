// Copyright - Archie Jaskowicz


#include "SoftwareOCSubsystem.h"

#include "SceneViewExtension.h"
#include "SoftwareOCSettings.h"
#include "Runtime/Renderer/Private/ScenePrivate.h"
#include "Runtime/Renderer/Private/SceneRendering.h"
#include "Unreal/SceneSoftwareOcclusion.h"

void USoftwareOCSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if(GetWorld() && GetLocalPlayer() && GetLocalPlayer()->GetPlayerController(GetWorld()))
	{
		PlayerCameraManager = GetLocalPlayer()->GetPlayerController(GetWorld())->PlayerCameraManager;
	}

	OcclusionSceneViewExtension = FSceneViewExtensions::NewExtension<FOcclusionSceneViewExtension>();
	OcclusionSceneViewExtension->OcSubsystem = this;
}

void USoftwareOCSubsystem::Deinitialize()
{
	Super::Deinitialize();

	PlayerCameraManager = nullptr;
}

void USoftwareOCSubsystem::PlayerControllerChanged(APlayerController* NewPlayerController)
{
	Super::PlayerControllerChanged(NewPlayerController);

	PlayerCameraManager = NewPlayerController->PlayerCameraManager;
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
		if (!IsValid(Component) || !Component->GetStaticMesh())
		{
			continue;
		}
		
		IDToMeshComp.Add(Component->GetPrimitiveSceneId().PrimIDValue, Component);
	}
}
