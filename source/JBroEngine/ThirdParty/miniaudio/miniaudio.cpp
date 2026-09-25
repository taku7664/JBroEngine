// miniaudio 의 구현은 이 번역 단위 하나에서만 컴파일된다(ThirdParty/README.md).
// JBroAudio(엔진 믹서)·JBroAsset(디코더)·JBroPlatform(출력 장치)이 모두 이 라이브러리를 링크하므로
// 구현이 둘이면 중복 정의다. 설정 매크로는 JBro.Common.props 의 JBroMiniaudioDefines 한 곳에 있다 -
// 구조체의 모양이 그 매크로에 따라 달라지므로 쓰는 쪽과 여기가 같은 값을 봐야 한다.

// miniaudio 는 Vorbis(OGG) 디코더를 내장하지 않는다. stb_vorbis 의 선언을 구현보다 먼저 보이면
// miniaudio 가 그것을 ma_decoder 의 Vorbis 백엔드로 잇는다.
#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244 4245 4456 4457 4701 4996)
#endif

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

// stb_vorbis 의 본체는 miniaudio 구현 뒤에 한 번 컴파일한다.
#undef STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
