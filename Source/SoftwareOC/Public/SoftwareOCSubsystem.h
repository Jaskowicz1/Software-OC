// Copyright - Archie Jaskowicz.

#pragma once

#include "CoreMinimal.h"
#include "OcclusionSceneView.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "SoftwareOCSubsystem.generated.h"

UCLASS()
class SOFTWAREOC_API USoftwareOCSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	virtual TStatId GetStatId() const override;

	virtual bool IsTickable() const override;
	
	virtual void Tick(float DeltaTime) override;

	// We store as uint32 because UPROPERTY doesn't like FPrimitiveComponentId
	UPROPERTY()
	TMap<uint32, TObjectPtr<UMeshComponent>> IDToMeshComp;

	void ForceUpdateMap();

private:
	
	TSharedPtr< class FOcclusionSceneViewExtension, ESPMode::ThreadSafe > OcclusionSceneViewExtension;
};
