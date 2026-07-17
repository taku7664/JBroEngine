#pragma once

#include <cstdint>

enum class ECanvasSerializeResult
{
	Success,
	InvalidArgument,
	IoError,
	ParseError
};

enum class ELayerSerializeResult
{
	Success,
	InvalidArgument,
	IoError,
	ParseError,
	// 파일의 오브젝트 guid 가 이미 캔버스에 살아 있다 — 같은 레이어 파일을 한 캔버스에 두 번
	// 로드하려는 경우다. guid 를 재발급해 넘어가면 "레이어 파일의 guid 는 영속" 계약이
	// 깨지므로(승계·Ref·뷰포트 카메라 지목이 전부 그 guid 를 믿는다) 로드 자체를 거부한다.
	DuplicateInstance
};

enum class ECanvasSimulationState
{
	Edit,
	Playing,
	Paused,
};
