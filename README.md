# cesium-procedural-foliage

This is an example project based on the tutorial: https://cesium.com/learn/unreal/unreal-procedural-foliage/ .

## Requirements:
- Unreal Engine 4.27
- The Cesium for Unreal plugin.
- DX12-capable graphics card and OS.
- Microsoft Visual Studio 2019

## Setting it up
1. Right click on `aiden_geo_tutorial.uproject` and click 'Generate Visual Studio Project Files'.
2. Open aiden_geo_tutorial.sln and click on `Local Windows Debugger`. This will build the solution and start the Unreal Editor.
3. Set your Cesium ION token in the Cesium 3D tileset actor.
4. After clicking Play, foliage instances should now be spawning in.

## Troubleshooting and optimisations
### Please see https://cesium.com/learn/unreal/unreal-procedural-foliage/#optimizations--troubleshooting

## Changes in this repo (DD/MM/YYYY)

### 19/12/2021
```
1. Fixed #3 bug, system should balance instances across ISMs.
2. Improve world origin rebasing support
```

### 10/12/2021
```
1. Upgraded project to support cesium-unreal 1.7.0 to 1.8.1
```

### 20/10/2021
```
1. Set DX12 as the default RHI
2. Fix unusual startup crash by disabling auto-activate on the Niagara component.
```