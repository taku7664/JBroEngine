---
name: jbro-guide
description: Create contextual JBroEngine guides for how-to and where-is questions, using the running editor's supported actions and selected objects.
---

# JBro guide authoring

Use when the user wants to learn a workflow or locate a feature. When they ask you to perform an edit, use jbro-create instead. Interpret intent from the conversation; do not route using a fixed keyword list.

1. Read `jbro_context` and inspect relevant objects with `jbro_object_read`. The `scope` identifies the current project/canvas generation. Numeric object IDs are decimal strings containing **InstanceId**, not EditorObjectId. Names are labels, never identity.
2. Read `jbro_guide_catalog`. It is the authoritative action/argument/route/ending catalog for this engine version. Select actions that accomplish the goal and compose a new guide. Do not limit yourself to built-in guide examples.
3. Author YAML using the contract below. Every step must explain the action and its purpose in the user's language. For a location question use the shortest supported path; for a workflow split meaningful actions into steps.
4. Call `jbro_guide_validate`. Correct errors using the catalog and returned line/reason. If the same cause fails twice, stop retrying and explain the limitation. Do not silently omit required steps.
5. Call `jbro_guide_start` with the current scope. The user performs the editing steps; you must not perform those same edits through mutation tools. Guide activation itself is allowed.
6. Use `jbro_guide_status` only when needed for a follow-up. Do not claim completion from parsing or starting the guide. Explain unsupported portions explicitly.

## Guide YAML

```yaml
Id: contextual_guide
Title:
  Key: guide.generated.contextual_guide.title
  String: Add a component
  Loc: en-US
Steps:
  - Id: pick
    Do: object.select
    Object: 4295032833
    Title:
      Key: guide.generated.contextual_guide.pick.title
      String: Select the object
      Loc: en-US
    Body:
      Key: guide.generated.contextual_guide.pick.body
      String: Select the highlighted object to inspect its components.
      Loc: en-US
  - Do: component.add
    Object: 4295032833
    Component: Collider2D
    RetreatTo: pick
    Title:
      Key: guide.generated.contextual_guide.add.title
      String: Add a collider
      Loc: en-US
    Body:
      Key: guide.generated.contextual_guide.add.body
      String: Add Collider2D from the component list.
      Loc: en-US
```

Replace the example object ID, component and locale using live data. `Key`, `String`, `Loc` are required together for Title and Body. Korean prose uses `Loc: ko-KR`. `Do` is an action, never a widget path or translated label. Optional `Via` must be a route in the catalog; omit it to let the engine choose a working route.

For objects created during a guide, give the producing step `Id: made` and use `Object: $made` in subsequent steps. For field-edit steps use the catalog's arguments and YAML `Value` when a specific value is required. Preserve real completion conditions; do not add a Next button that bypasses required work.

## Existing capability limits

- A guide cannot currently highlight both source and destination of a drag. Chat attachments and drag instruction steps are separate capabilities.
- The guide format cannot address a second component of the same type. Do not confuse direct editing tools' component InstanceId with guide component selection.
- Unsupported menu items, deep fields and script-editor steps cannot be invented as guide actions. Explain the missing coverage and give an accurate textual continuation.
- Do not fix or bypass the guide engine through shell commands, scene-file edits or new action names.

## Explicit attachments

The turn context includes `attachments` separately from the current `selection`. An explicit attachment identifies the user's subject even if selection later changes. Read its kind and IDs: object `instanceId`, component/script owning `instanceId` plus `componentId`, asset `assetId`. Refresh the context scope and query object details with `jbro_object_read` or asset metadata with `jbro_asset_read`. Attachments are references, not embedded source or image pixels. Reject invalid references instead of substituting an equal name. Ask only if several attached subjects leave the goal ambiguous.

`jbro_context` keeps the current turn's explicit attachments and refreshes their validity and asset metadata. Use that response if a resumed thread does not expose the newer `jbro_asset_read` tool. Do not create another conversation just to access attachment metadata.
