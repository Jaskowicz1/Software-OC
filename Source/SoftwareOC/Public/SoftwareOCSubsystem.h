// Copyright - Archie Jaskowicz

#pragma once

#include "CoreMinimal.h"
#include "OcclusionSceneView.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "SoftwareOCSubsystem.generated.h"

UCLASS()
class SOFTWAREOC_API USoftwareOCSubsystem : public ULocalPlayerSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	virtual void PlayerControllerChanged(APlayerController* NewPlayerController) override;

	virtual TStatId GetStatId() const override;
	
	virtual void Tick(float DeltaTime) override;

private:
	
	UPROPERTY()
	APlayerCameraManager* PlayerCameraManager;

	TSharedPtr< class FOcclusionSceneViewExtension, ESPMode::ThreadSafe > OcclusionSceneViewExtension;
};
