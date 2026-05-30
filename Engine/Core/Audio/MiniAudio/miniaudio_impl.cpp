// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  miniaudio 의 구현은 이 한 파일에서만 컴파일된다.
//
//  miniaudio.h 가 Engine/ThirdParty/miniaudio/ 에 추가되어 있고
//  JBRO_HAS_MINIAUDIO=1 이 정의되어 있을 때만 실제 코드가 활성화.  그렇지
//  않으면 빈 컴파일 단위.
//
//  이 파일은 pch.h 를 사용하지 않는다 — miniaudio 의 implementation 매크로가
//  표준 헤더 includes 순서에 민감하기 때문.  vcxproj 에서 이 cpp 만 "Use" 가
//  아닌 "Not Using Precompiled Headers" 로 설정되어야 한다.
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

#if defined(JBRO_HAS_MINIAUDIO) && JBRO_HAS_MINIAUDIO

// ── stb_vorbis 통합 ────────────────────────────────────────────────────────
// miniaudio 는 Vorbis (OGG) 디코더를 내장하지 않는다 — stb_vorbis.c 를
// MA_IMPLEMENTATION 전에 inline 하면 miniaudio 가 자동으로 ma_decoder 에
// Vorbis 백엔드를 연결한다.  이 한 파일에서만 정의되도록 묶어둔다.
#define STB_VORBIS_HEADER_ONLY
#include "ThirdParty/stb/stb_vorbis.c"

// MSVC 가 stb_vorbis 의 일부 경고를 너무 시끄럽게 띄우는 것 억제 (외부 코드).
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4244 4245 4456 4457 4701 4996)
#endif

#define MINIAUDIO_IMPLEMENTATION
#include "ThirdParty/miniaudio/miniaudio.h"

// stb_vorbis 의 본체(implementation) 는 한 번만 컴파일 — MA_IMPLEMENTATION
// 이후에 둔다.
#undef STB_VORBIS_HEADER_ONLY
#include "ThirdParty/stb/stb_vorbis.c"

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#endif
