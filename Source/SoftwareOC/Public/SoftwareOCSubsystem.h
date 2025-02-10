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

	// Objects that we've culled.
	// This will always return true (false objects are removed). Using Map on purpose for scaling reasons.
	TMap<FPrimitiveComponentId, bool> CachedVisibilityMap;

	// This is purely for objects that are meant to be hidden by the game/user
	// This prevents cameras and whatnot from showing when they're not meant to.
	TMap<FPrimitiveComponentId, bool> CachedHiddenMap;

	void ForceUpdateMap();

private:
	
	TSharedPtr< class FOcclusionSceneViewExtension, ESPMode::ThreadSafe > OcclusionSceneViewExtension;
};
