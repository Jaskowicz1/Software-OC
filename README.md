# Software-OC
Software Occlusion Culling | In Unreal Engine 5

CPU Occlusion Culling for UE5, benefiting games that may have a major GPU bottleneck and have the frame budget to offload to the CPU.

## Installation

- Download the plugin (Ideally from the Releases tab at the right side, if it exists).
- Place it in your Project's "Plugins" folder.
- Open your project
- Turn on the Plugin
- Open your Project Settings and look for `Occlusion Culling`
- Turn off Unreal's `Occlusion Culling` under `Engine - Rendering` (This will replace Unreal's GPU Occlusion Culling)
- Now look for `Software Occlusion Culling`
- Tick `Enable Occlusion`
- Play!