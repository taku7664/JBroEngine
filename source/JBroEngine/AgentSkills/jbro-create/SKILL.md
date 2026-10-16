---
name: jbro-create
description: Perform requested JBroEngine scene edits and C++ game-script creation through live engine tools, preserving command undo and source conflicts.
---

# JBro creation and editing

Use for requests to perform work. Use jbro-guide for learning and location questions; do not edit the scene simply because the question mentions an editing operation.

Read `jbro_context` and `jbro_catalog` first. Inspect the relevant object before deciding which component or field to change. Ask for the target only if the current selection and conversation do not identify it. IDs are decimal strings; retain the project/canvas `scope` and refresh on changes. Never resolve a missing object by finding another with the same name.

## Scene edits

Use the object/component/field tools. They execute the editor's existing undoable commands. A field write requires the exact `expected` YAML from `jbro_object_read` and the attached component's **InstanceId**. If the object or value changed, read it again and reconsider; do not blindly retry. A component type must come from the live attachable catalog.

Do not edit `.jcanvas`, `.jproject`, asset metadata or engine source files. Do not bypass a blocked command, game execution or debugger lock. Stop immediately when the user cancels. Partial edits remain available to the editor's normal undo history; report them honestly.

## C++ scripts

1. List existing scripts and read relevant project examples. `jbro_script_create` generates a valid C++ script pair and prepares the existing project. Do not reproduce module registration boilerplate manually.
2. Inspect the generated files and current engine declarations with `jbro_script_reference`. `source: engine` paths are relative to the installed engine root. Useful declarations include:
   - `Modules/JBroRuntime/Include/JBro/Runtime/GameObjectHandle.h`
   - `Modules/JBroRuntime/Include/JBro/Runtime/GameScriptBase.h`
   - `Modules/JBroFramework2D/Include/JBro/Framework2D/Handle/Transform2D.h`
   Read the relevant handle/header before using a method; these pointers are starting points, not a complete API catalog.
3. Use `jbro_script_read` to obtain text, a write token and unsaved status. Modify only clean documents with `jbro_script_write`. A conflict is a reason to stop and let the user resolve their edits; never discard or automatically save unrelated work.
4. Use engine value types (`Float`, `Bool`, `Int32`, `UInt64`), handle APIs, and generated script macros. Do not include engine component data headers in game scripts. Keep the existing framework dimension and file style.
5. When asked to implement functioning script behavior, start `jbro_script_build` and check `jbro_script_diagnostics`. `state` is Idle/Running/Succeeded/Failed as specified in the tool schema. A running build is not success. Do not launch several concurrent builds. Fix errors only within the requested change; if the same cause fails twice, report it rather than repeatedly trying variants.
6. If the user requests source writing only, do not silently expand into running the game. Report source changes, saved files, build status and runtime verification separately.

JBroScript generation follows the existing engine's availability. Do not assume that an emitter or runtime feature exists without checking this installation. The working default for executable game scripts is the generated C++ project.

Never use a generic shell, external MCP write tools or direct filesystem changes instead of JBro tools. A disconnected/interrupted edit must not be replayed automatically. Tool success confirms that operation only, not the entire user's goal.
