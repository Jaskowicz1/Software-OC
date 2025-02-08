// Copyright - Archie Jaskowicz.

#pragma once

#include "CoreMinimal.h"
#include "SoftwareOCSettings.generated.h"

/**
 * 
 */
UCLASS(Config=SoftwareOC, DefaultConfig, meta=(DisplayName="Software Occlusion Culling"))
class SOFTWAREOC_API USoftwareOCSettings : public UDeveloperSettings
{
	GENERATED_BODY()
	
public: /* Variables */

	/**
	 * @brief If this is off, you will default back to GPU Occlusion culling.
	 */
	UPROPERTY(Config, EditAnywhere, BlueprintReadOnly, Category="Software OC")
	bool bEnableOcclusion;
};
