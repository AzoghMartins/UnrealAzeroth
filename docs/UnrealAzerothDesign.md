# UnrealAzeroth Design

Status: Draft 0.1

## Purpose

`UnrealAzeroth` is intended to be an Unreal Engine plugin that:

- talks to a running AzerothCore server using the same network protocol family as a native WoW 3.3.5a client
- reads user-provided WoW 3.3.5a client data from local MPQ archives
- decodes Blizzard asset formats and renders them in Unreal
- exposes clean Unreal-facing APIs for gameplay, tooling, and editor workflows

This is not a thin import plugin. It is a protocol, asset, and rendering bridge
between Unreal and the WoW 3.3.5a ecosystem.

## Core Constraints

### Protocol compatibility is packet-level

AzerothCore is a WoW 3.3.5a server implementation, not an HTTP service. The
plugin therefore needs client-compatible auth and world session behavior instead
of a bespoke REST or RPC layer.

Implications:

- the plugin must handle the auth flow, realm selection, world connection, and
  packet encryption/serialization expected by a 3.3.5a-compatible client
- the plugin must treat the auth socket and world socket as first-class runtime
  systems
- protocol support should be phased, starting with login and minimal world
  presence before broader gameplay coverage

### Asset loading is based on WoW 3.3.5a client data

Wrath-era client assets live in MPQ archives and use WoW-specific formats.
Initial asset support should target:

- `BLP` textures
- `M2` models
- `WMO` world map objects

Later phases may add:

- `ADT` terrain tiles
- `WDT` world definitions
- DBC-driven metadata required for richer content binding

### The plugin must not redistribute Blizzard assets

The plugin should only operate on user-provided local client data.

Requirements:

- no WoW client files in the repository
- no packaged Blizzard assets in plugin releases
- all asset discovery must start from a configured local client data path

### Keep licensing clean

This plugin must not casually absorb GPL server code into a separate plugin
repository.

Rules:

- use AzerothCore as a behavioral and interoperability target, not as a source
  dump to copy into the plugin
- if code is reused from permissive projects, preserve notices and track origin
- prefer implementing protocol and format handling behind our own interfaces

## Product Goals

### Primary goals

- connect Unreal to a live AzerothCore realm using native-compatible 3.3.5a
  client protocol behavior
- load and render WoW assets directly from MPQ-backed client data
- provide stable C++ and Blueprint APIs for session state, entities, and asset
  access
- support both editor-time workflows and runtime streaming

### Non-goals for the first versions

- full gameplay parity with the retail 3.3.5a client
- immediate support for every opcode, subsystem, and shader path
- automatic support for non-3.3.5a clients
- bundling WoW data into Unreal assets by default

## Proposed Architecture

The current plugin has one runtime module and one editor module. That is fine
for the scaffold, but it is too flat for the final system. The design should
aim for these logical subsystems, whether they remain namespaces inside the
runtime module at first or become separate UE modules later.

### Near-term code layout

Until the plugin is large enough to justify more UE modules, the current
runtime module should be organized by folders and namespaces:

- `Source/UnrealAzeroth/Public/Session`
- `Source/UnrealAzeroth/Public/Assets`
- `Source/UnrealAzeroth/Private/Net`
- `Source/UnrealAzeroth/Private/Protocol`
- `Source/UnrealAzeroth/Private/World`
- `Source/UnrealAzeroth/Private/Archives`
- `Source/UnrealAzeroth/Private/Formats`
- `Source/UnrealAzeroth/Private/Rendering`

The editor module should mirror that split only where editor-only code is
required, such as settings panels, preview tooling, and import commandlets.

### Runtime subsystems

#### `AzerothSession`

Owns the live connection lifecycle.

Responsibilities:

- auth server connection
- realm list retrieval and selection
- world server connection
- session state machine
- login result and disconnect reporting

Recommended Unreal surface:

- `UGameInstanceSubsystem` as the top-level session entry point
- async delegates for connect, auth, realm list, character list, and disconnect

#### `AzerothProtocol`

Owns packet definitions, serializers, deserializers, and opcode dispatch.

Responsibilities:

- auth packet codec
- world packet framing
- inbound opcode registry
- outbound packet builders
- crypto/encryption integration

Key rule:

- parsing logic must remain independent from Unreal actor or rendering concerns

#### `AzerothWorld`

Maps network state into Unreal-friendly gameplay state.

Responsibilities:

- GUID/object tracking
- object create/update/remove handling
- mapping server entities to Unreal actors or lightweight runtime proxies
- movement and visibility updates

This layer should start minimal. The first milestone only needs enough world
state to prove that the network protocol bridge is working.

### World actor strategy

Do not build the world layer around nested child actors. That pattern becomes
heavy and awkward once a zone contains large numbers of doodads, foliage,
gameobjects, creatures, and players.

Use this shape instead:

- one reusable model/render component that knows how to resolve Azeroth asset
  references
- thin actor subclasses that describe world behavior and ownership
- later, specialized managers for batched foliage and repeated static props

Recommended actor split:

- `AUnrealAzerothStaticAssetActor` for buildings, furniture, and general static
  props
- `AUnrealAzerothFoliageActor` for trees and larger natural set dressing
- `AUnrealAzerothGameObjectActor` for server-tracked interactive objects
- `AUnrealAzerothUnitActor` for NPCs and mobs
- `AUnrealAzerothPlayerActor` for remote player representations

Why this split:

- static/foliage/gameobject content is rendered differently from animated
  units, so the actor tree should acknowledge that early
- gameobjects and units are usually server-driven, while many static props are
  editor-placed
- a shared render/model component keeps the future MPQ loader in one place
  instead of duplicating asset logic across actor classes

#### `AzerothArchives`

Provides a virtual file system over local MPQ archives.

Responsibilities:

- mount MPQ sets in correct precedence order
- locale-aware file resolution
- file existence and byte-stream access
- archive metadata and cache keys

This layer should be engine-agnostic and expose plain byte buffers or stream
interfaces to the higher asset decoders.

#### `AzerothAssets`

Decodes WoW-specific formats into engine-neutral intermediate data.

Responsibilities:

- BLP decode to raw texture surfaces
- M2 parse to mesh, skeleton, animation, and material descriptors
- WMO parse to root/group/doodad descriptors
- later, terrain/world file parsing

Important rule:

- parsers should not construct `UObject`s directly
- parsing and Unreal asset creation should stay separate

#### `AzerothRendering`

Converts intermediate asset data into Unreal renderable assets or runtime
proxies.

Responsibilities:

- build `UTexture2D` from decoded BLP surfaces
- build `UStaticMesh` or runtime mesh data for static M2/WMO content
- build `USkeletalMesh`, `USkeleton`, and animations for animated M2 content
- translate WoW material state into Unreal material instances

This layer will carry most of the compatibility debt. It should therefore be
introduced in phases instead of attempting perfect visual parity immediately.

### Editor subsystem

#### `AzerothEditor`

Owns user-facing tools and offline workflows.

Responsibilities:

- plugin settings for client data path and server endpoints
- asset preview and import utilities
- diagnostics, validation, and commandlets
- optional cooked/prebuilt asset workflows

The editor module should never contain protocol logic or format parsers that
the runtime requires.

## Data Flow

### Network flow

1. User or game code requests connection through `AzerothSession`.
2. The plugin connects to the auth server.
3. Auth handshake completes and the realm list is requested.
4. The selected realm yields world endpoint details.
5. The plugin connects to the world server.
6. World packets are decrypted, framed, decoded, and dispatched.
7. `AzerothWorld` updates runtime entity state.
8. Unreal-facing systems react through delegates, subsystems, and actor bridges.

### Asset flow

1. User configures the local WoW 3.3.5a client data directory.
2. `AzerothArchives` mounts the MPQ set and exposes a virtual file system.
3. `AzerothAssets` reads target files such as `BLP`, `M2`, or `WMO`.
4. Parsers emit intermediate structures.
5. `AzerothRendering` converts those structures into Unreal assets or runtime
   render data.
6. Results are cached and reused through deterministic cache keys.

## Format Strategy

### BLP

This should be the first completed asset pipeline.

Why:

- it is the simplest visible win
- textures are needed by every later asset type
- it lets us validate archive mounting, path resolution, mip handling, and
  Unreal texture creation early

Target result:

- reliable decode of the BLP variants encountered in 3.3.5a data
- conversion to `UTexture2D`
- cache reuse without reparsing on every load

### M2

M2 is the most important model format for creatures, characters, doodads, and
many gameplay-facing assets.

Implementation should be phased:

- Phase A: static mesh extraction for simple previews
- Phase B: skeleton and skinning support
- Phase C: animation import/runtime playback
- Phase D: particles, ribbons, and more complex render-state behavior

Do not aim for full shader parity in the first pass. Start with a conservative
material translation table that gets geometry on screen correctly.

### WMO

WMO support should focus on:

- root/group relationship parsing
- geometry assembly
- material/texture binding
- doodad placement hooks
- portal/interior metadata retention for future use

WMO rendering can initially target a usable visual result before attempting full
indoor lighting and visibility behavior parity.

## Unreal Integration Strategy

### Public Unreal API

The first stable Unreal-facing API should stay small:

- connect and disconnect session
- enumerate realms and characters
- receive world join state
- request asset load by WoW client path
- receive renderable or importable Unreal assets

Blueprint exposure should be selective. Do not dump packet-level structures into
Blueprints.

### Threading model

The plugin will need strict thread separation:

- network I/O off the game thread
- archive reads and format parsing off the game thread
- `UObject` creation marshaled back to the game thread

A common failure mode for this plugin would be letting low-level parsing and
network callbacks leak directly into gameplay code on the wrong thread.

### Caching

We need both runtime and editor-aware caching.

Cache keys should include at least:

- client file path
- archive/build identity
- plugin importer version
- relevant import options

Preferred behavior:

- parsed intermediate data may be cached in memory
- converted Unreal assets should use the Derived Data Cache where practical
- cache invalidation should be explicit and deterministic

## Recommended External Dependencies

These should remain replaceable behind our own abstractions.

### MPQ access

Recommended default:

- `StormLib`

Reason:

- mature MPQ support
- Linux-friendly build path
- permissive license

### Protocol reference

Recommended reference sources:

- AzerothCore behavior and packet compatibility testing
- permissive client-side reference projects where useful

The key point is to keep our implementation under our own boundaries instead of
embedding server internals into the plugin.

## Milestone Plan

### Milestone 0: Foundations

- establish coding conventions and folder structure
- add automated plugin packaging in CI
- add golden-test fixtures for protocol and asset parsing
- define plugin settings object for client path and server connection

### Milestone 1: MPQ and BLP

- mount MPQ archives from a local 3.3.5a client installation
- resolve file paths correctly
- decode BLP textures into Unreal textures
- build a simple texture preview tool

### Milestone 2: Auth and Realm Login

- implement auth connection flow
- retrieve realm list
- connect to a realm
- enumerate characters

Success condition:

- Unreal can authenticate to a running AzerothCore server and reach character
  selection with native-compatible protocol behavior

### Milestone 3: Minimal World Session

- enter the world with a chosen character
- process a minimal safe set of world packets
- track nearby object create/update/remove events
- expose a diagnostic view of live server state

### Milestone 4: M2 Rendering

- parse M2 geometry and materials
- render simple static M2 content in Unreal
- add first skeletal/animated prototype

### Milestone 5: WMO Rendering

- parse WMO root and group files
- render WMO geometry with textures
- support doodad references at least in a first pass

### Milestone 6: World Building and Streaming

- begin terrain/world integration if needed
- stream content based on server state or explicit editor requests
- optimize cache and import costs

## Key Risks

### 1. Protocol edge cases

The plugin may connect successfully but fail on less obvious world packet flows.
This is why protocol support must be backed by captured fixtures and integration
tests against a local AzerothCore environment.

### 2. Asset format fidelity

Getting geometry loaded is much easier than matching original rendering
behavior. Material translation, animation behavior, and special render passes
will take multiple iterations.

### 3. Licensing mistakes

Blindly copying GPL server code into the plugin would create avoidable legal and
distribution constraints.

### 4. Runtime performance

Naive parsing and conversion on the game thread will make the plugin unusable.
Archive I/O, decode, and conversion must be designed around async execution and
cache reuse from the beginning.

## Open Questions

- Is the target primarily an in-editor world viewer, a runtime game client, or
  both from the start?
- Do we want runtime-on-demand asset loading only, or editor-time import and
  cook workflows as a primary path?
- How much of character control, UI, and gameplay do we want to emulate versus
  replacing with native Unreal systems?
- Do we want strict direct-protocol support only, or a debug-only proxy/bridge
  path for testing and packet inspection?

## Initial Recommendation

The first implementation slice should be:

1. MPQ mounting
2. BLP decoding into `UTexture2D`
3. auth + realm + character list connection to AzerothCore

That sequence proves the two hardest foundations independently:

- client data access works
- client/server protocol compatibility works

Only after those two foundations are stable should we commit to full M2/WMO
rendering or broader world replication work.

## References

- AzerothCore server repository: https://github.com/azerothcore/azerothcore-wotlk
- AzerothCore web client reference: https://github.com/azerothcore/acore-client
- StormLib MPQ library: https://github.com/ladislav-zezula/StormLib
- WoWDev MPQ notes: https://wowdev.wiki/MPQ
- WoWDev BLP notes: https://wowdev.wiki/BLP
- WoWDev M2 notes: https://wowdev.wiki/M2
- WoWDev WMO notes: https://wowdev.wiki/WMO
