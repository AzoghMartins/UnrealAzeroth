# UnrealAzeroth

`UnrealAzeroth` is a standalone Unreal Engine plugin repository.

The repository root is the plugin root. It is intentionally separate from the
local Unreal Engine source checkout so engine updates and plugin development
stay isolated from each other.

## Layout

- `UnrealAzeroth.uplugin`: plugin descriptor
- `Source/UnrealAzeroth`: runtime module
- `Source/UnrealAzerothEditor`: editor-only module

## Local Build

Build and package the plugin against the local engine source build:

```bash
/home/azoghmartins/UE/UnrealEngine/Engine/Build/BatchFiles/RunUAT.sh \
  BuildPlugin \
  -Plugin=/home/azoghmartins/UE/UnrealAzeroth/UnrealAzeroth.uplugin \
  -Package=/tmp/UnrealAzerothPackage \
  -TargetPlatforms=Linux
```

## Usage

For project usage, place or symlink this repository under:

```text
<ProjectRoot>/Plugins/UnrealAzeroth
```

Keeping the plugin in its own repository makes it easy to reuse across
projects without mixing it into the engine checkout.
