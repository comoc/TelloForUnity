# TelloForUnity

A Ryze Tech/DJI Tello application development resources for Unity.

## Note

This project is including the following modules.

* Forked and modified source code of [TelloLib](https://github.com/comoc/TelloLib). The original version is [Kagrathea's TelloLib](https://github.com/Kragrathea/TelloLib).
* Modified source code of [NativeRenderingPlugin](https://bitbucket.org/Unity-Technologies/graphicsdemos/src/default/NativeRenderingPlugin/)
* Pre-built DLLs for Windows 64 bit of [FFmpeg](https://www.ffmpeg.org/)

At this time, Windows 64 bit and macOS 64 bit versions are supported.
Android (including Oculus Go) version is experimentally supported.

## Installation

### Clone

```
git clone https://github.com/comoc/TelloForUnity.git
cd TelloForUnity
git submodule update --init --recursive
```

### Windows Specific Settings

Please confirm the Windows Firewall settings of your Unity Editor. Public network access is required to receive the video stream from Tello.

![image.png](https://qiita-image-store.s3.amazonaws.com/0/39561/6e7de478-cbd8-be4f-1687-2f43135f9c10.png)

### macOS Specific Settings

Before using, ```brew install x264 ffmpeg``` is required.

### Oculus Go Specific Settings

Scenes/OculusGo is an example. Before opening the scene, [Oculus Utilities For Unity](https://developer.oculus.com/downloads/unity/) should be imported.

## Basic Usage for Windows or macOS

* Open the TelloUnityDemo project into Unity Editor.
* Edit > Project Settings > Player > Other Settings > "Scripting Runtime Version" as ".NET 4.x Equivalent".
* Open the Scenes/Master scene.
* Connect to Tello Wi-Fi.
* Play.
* Key assignments and Tello's battery status are displayed in the Game window.

## Legal Information

* [FFmpeg License and Legal Considerations](https://www.ffmpeg.org/legal.html)
