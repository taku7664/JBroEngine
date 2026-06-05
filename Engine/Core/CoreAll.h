#pragma once

// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  CoreAll.h ─ 엔진/에디터 내부용 Core 묶음 umbrella
//
//  엔진/에디터 cpp 가 Core 서비스(Logger / FileSystem / Input / RHI / Asset
//  / Task / Time / Math 등)를 함께 만지는 경우가 많아 한 줄로 가져온다.
//
//    #include "Core/CoreAll.h"
//
//  스크립트 사용자는 이 헤더 대신 ScriptAPI.h umbrella 를 사용한다.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

// ── 글로벌 서비스 번들 ──────────────────────────────────────────────────
#include "Core/EngineCore.h"
#include "Core/ScriptCore.h"
#include "Core/Engine.h"

// ── 로깅 / 디버그 ───────────────────────────────────────────────────────
#include "Core/Logging/Logger.h"
#include "Core/Debug/Debug.h"
#include "Core/Debug/DebugDraw.h"
#include "Core/Debug/DebugDraw2D.h"
#include "Core/Debug/DebugRenderer.h"
#include "Core/Debug/DebugRenderer2D.h"

// ── 파일/리소스/로컬라이즈 ─────────────────────────────────────────────
#include "Core/FileSystem/FileSystem.h"
#include "Core/Localization/LocalizationManager.h"
#include "Core/Resource/ResourceRegistry.h"

// ── 입력 / 시간 / 수학 / 랜덤 ───────────────────────────────────────────
#include "Core/Input/Input.h"
#include "Core/Input/IInputMessageHandler.h"
#include "Core/Time/Time.h"
#include "Core/Math/MathService.h"
#include "Core/Random/RandomService.h"

// ── 모듈 / 게임 모듈 인터페이스 ────────────────────────────────────────
#include "Core/Module/Module.h"
#include "Core/Game/GameModuleTypes.h"
#include "Core/Game/IGameModule.h"

// ── 자산 시스템 ─────────────────────────────────────────────────────────
#include "Core/Asset/AssetTypes.h"
#include "Core/Asset/IAsset.h"
#include "Core/Asset/IAssetLoader.h"
#include "Core/Asset/IAssetRegistry.h"
#include "Core/Asset/IAssetManager.h"
#include "Core/Asset/FileAsset.h"
#include "Core/Asset/SpriteAsset.h"
#include "Core/Asset/MaterialAsset.h"

// ── 플랫폼 / RHI / 렌더러 / 태스크 / 네트워크 ─────────────────────────
#include "Core/Platform/IPlatform.h"
#include "Core/RHI/IRHIDevice.h"
#include "Core/RHI/IRHITexture.h"
#include "Core/RHI/IRHISampler.h"
#include "Core/Renderer/RenderScene.h"
#include "Core/Renderer/RenderResources2D.h"
#include "Core/Renderer/Forward2DRenderer.h"
#include "Core/Task/TaskManager.h"
#include "Core/Network/NetworkTypes.h"
#include "Core/Network/INetworkManager.h"
