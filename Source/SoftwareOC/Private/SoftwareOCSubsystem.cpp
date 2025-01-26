// Copyright - Archie Jaskowicz


#include "SoftwareOCSubsystem.h"

#include "SoftwareOCSettings.h"
#include "Runtime/Renderer/Private/SceneRendering.h"
#include "Unreal/SceneSoftwareOcclusion.h"

static FSceneSoftwareOcclusion* SceneSoftwareOcclusion;

void USoftwareOCSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	SceneSoftwareOcclusion = new FSceneSoftwareOcclusion{};

	if(GetWorld() && GetLocalPlayer() && GetLocalPlayer()->GetPlayerController(GetWorld()))
	{
		PlayerCameraManager = GetLocalPlayer()->GetPlayerController(GetWorld())->PlayerCameraManager;
	}
}

void USoftwareOCSubsystem::Deinitialize()
{
	Super::Deinitialize();

	PlayerCameraManager = nullptr;
	delete SceneSoftwareOcclusion;
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
	const USoftwareOCSettings* OcclusionSettings = GetDefault<USoftwareOCSettings>();

	if (!OcclusionSettings->bEnableOcclusion)
	{
		return;
	}

	if (!GetLocalPlayer() || !PlayerCameraManager || !GetWorld())
	{
		return;
	}

	FSceneViewFamilyContext ViewFamily(FSceneViewFamily::ConstructionValues(
		GetLocalPlayer()->ViewportClient->Viewport,
		GetWorld()->Scene,
		GetLocalPlayer()->ViewportClient->EngineShowFlags)
		.SetRealtimeUpdate(true));
	
	uint8 ViewBit = 0x1;
	for (int32 ViewIndex = 0; ViewIndex < ViewFamily.AllViews.Num(); ++ViewIndex, ViewBit <<= 1)
	{
		const FSceneView* SceneView = ViewFamily.AllViews[ViewIndex];
		if (!SceneView->bIsViewInfo)
		{
			continue;
		}

		// Sneaky way to bypass const because the constructor causes a compile fail.
		FViewInfo* View = (FViewInfo*)(&SceneView);
		SceneSoftwareOcclusion->Process(ViewFamily.Scene->GetRenderScene(), *View);
		
		ViewFamily.Views.Add(View);
	}
	
	//GetLocalPlayer()->GetWorld()->GetFirstPlayerController()->GetPlayerC
	//SceneSoftwareOcclusion->Process(GetScene());
}
