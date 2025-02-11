﻿// Copyright - Archie Jaskowicz.

#pragma once

#include "SceneViewExtension.h"

class USoftwareOCSubsystem;

class FOcclusionSceneViewExtension : public FSceneViewExtensionBase
{
public:
	FOcclusionSceneViewExtension(const FAutoRegister& AutoRegister);
	~FOcclusionSceneViewExtension();

	/** Scene View extension interface. */
public:
	virtual void SetupViewFamily(FSceneViewFamily& InViewFamily) override {};
	virtual void SetupView(FSceneViewFamily& InViewFamily, FSceneView& InView) override {};
	virtual void BeginRenderViewFamily(FSceneViewFamily& InViewFamily) override {};
	virtual void PreRenderViewFamily_RenderThread(FRDGBuilder& GraphBuilder, FSceneViewFamily& InViewFamily) override {};
	virtual void PreRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView) override {};
	virtual void PostRenderBasePassDeferred_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView, const FRenderTargetBindingSlots& RenderTargets, TRDGUniformBufferRef<FSceneTextureUniformParameters> SceneTextures) override;
	virtual void PostRenderView_RenderThread(FRDGBuilder& GraphBuilder, FSceneView& InView) override {};
	
	TArray<bool>* SubIsOccluded = nullptr;
	USoftwareOCSubsystem* OcSubsystem = nullptr;
};
