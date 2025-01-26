// Copyright - Archie Jaskowicz


#include "SoftwareOCSubsystem.h"

#include "Unreal/SceneSoftwareOcclusion.h"

void USoftwareOCSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	SceneSoftwareOcclusion = NewObject<FSceneSoftwareOcclusion>();
}

void USoftwareOCSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

void USoftwareOCSubsystem::Tick(float DeltaTime)
{
	GetLocalPlayer()->GetWorld()->GetFirstPlayerController()->GetPlayerC
	SceneSoftwareOcclusion->Process(GetScene());
}
