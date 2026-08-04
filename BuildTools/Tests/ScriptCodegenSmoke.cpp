// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
//  ScriptCodegenSmoke — JPROP 코드 생성기(GameScriptProjectGenerator)의 출력 검사.
//
//  왜 필요한가: 생성기는 **에디터 안에서만** 돌고, 그 출력은 사용자 프로젝트에서
//  컴파일된다. 즉 여기서 틀리면 에디터에는 아무 에러도 안 뜨고 사용자 빌드가 깨지거나
//  (더 나쁘게) 프로퍼티가 조용히 누락된다. 에디터를 띄우지 않고 그 경로를 검사한다.
//
//  임시 폴더에 최소 프로젝트를 만들고 스크립트 헤더 하나를 심은 뒤 EnsureProject 를
//  부르고, 생성된 GeneratedScriptRegistry.cpp 를 문자열로 확인한다.
//
//  종료 코드 = 실패한 검사 번호(0 = 전부 통과).
// ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

#include "Editor/Project/GameScriptProjectGenerator.h"
#include "Editor/Project/ScriptEnumScanner.h"

#include <clocale>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace
{
	// enum 하나 + 이번 세션에서 새로 열어 준 타입들(Enum / Color / Layout2D / Int32)을
	// 한꺼번에 선언한다. 하나라도 매핑이 빠지면 생성 출력에서 바로 드러난다.
	const char* const kScriptHeader = R"(#pragma once

#include "GameFramework/Scripting/ScriptAPI.h"

// 값이 연속이 아닌 enum — 이름표 테이블이 인덱스가 아니라 실제 값을 담는지 본다.
enum class ECodegenMode : std::uint8_t
{
	Idle,          // 0
	Charging = 5,  // 쉼표가 든 주석 — 주석 제거가 안 되면 없는 열거자가 하나 생긴다
	Firing   = 9,
};

JBRO_SCRIPT CCodegenProbe final : public GameScript
{
	SCRIPT_CLASS(CCodegenProbe)
public:
	JPROP() ECodegenMode Mode = {};
	JPROP() Color Tint = Color{ 1.0f, 1.0f, 1.0f, 1.0f };
	JPROP() Layout2D Anchor = {};
	JPROP() Int32 Hits = 0;
	JPROP() Int64 Score = 0;
};
)";

	bool WriteText(const std::filesystem::path& path, const std::string& content)
	{
		std::error_code errorCode;
		std::filesystem::create_directories(path.parent_path(), errorCode);
		std::ofstream file(path, std::ios::out | std::ios::trunc | std::ios::binary);
		if (false == file.is_open())
		{
			return false;
		}
		file << content;
		return file.good();
	}

	bool ReadText(const std::filesystem::path& path, std::string& outText)
	{
		std::ifstream file(path, std::ios::in | std::ios::binary);
		if (false == file.is_open())
		{
			return false;
		}
		outText.assign((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		return true;
	}

	int Fail(int code, const std::string& message)
	{
		std::cerr << "Script codegen smoke test failed (" << code << "): " << message << "\n";
		return code;
	}
}

// 인자를 하나라도 주면 생성 결과를 지우지 않고 경로를 찍는다. 생성된 .cpp 를 실제로
// 컴파일해 보는(이 테스트가 문자열만 보는 것과 별개인) 확인을 손으로 할 때 쓴다.
int main(int argc, char** /*argv*/)
{
	// 엔진 호스트(Engine/Application/Application.cpp:11)와 같은 계약 — 이 한 줄이
	// std::filesystem::path::string() 을 UTF-8 로 만든다. 없으면 생성기가 뱉는
	// .vcxproj 안의 경로가 한글 계정명에서 깨진다(실제로 여기서 걸렸다).
	std::setlocale(LC_ALL, ".UTF-8");

	const bool keepOutput = argc > 1;
	std::error_code errorCode;
	const std::filesystem::path repoRoot = std::filesystem::current_path();
	const std::filesystem::path root = std::filesystem::temp_directory_path(errorCode)
		/ "JBroScriptCodegenSmoke";

	std::filesystem::remove_all(root, errorCode);
	errorCode.clear();

	const std::filesystem::path contentPath = root / "Contents";
	const std::filesystem::path scriptPath  = contentPath / "Scripts";
	std::filesystem::create_directories(scriptPath, errorCode);
	if (errorCode)
	{
		return Fail(1, "cannot create the scratch project folder.");
	}

	if (false == WriteText(scriptPath / "CodegenProbe.h", kScriptHeader))
	{
		return Fail(2, "cannot write the probe script header.");
	}

	// ── 스캐너 단독 검사 ────────────────────────────────────────────────────
	// 저작 UI(ScriptSchema) 도 같은 함수를 쓰므로 여기서 깨지면 콤보도 같이 빈다.
	{
		const std::vector<ScriptEnumInfo> enums = ScanScriptEnums(scriptPath);
		if (1 != enums.size() || enums[0].ClassName != "ECodegenMode")
		{
			return Fail(3, "the enum scanner did not find exactly ECodegenMode.");
		}
		// 주석 안의 쉼표가 열거자로 새지 않아야 한다.
		if (3 != enums[0].Enumerators.size()
			|| enums[0].Enumerators[0] != "Idle"
			|| enums[0].Enumerators[1] != "Charging"
			|| enums[0].Enumerators[2] != "Firing")
		{
			return Fail(4, "enumerator names were parsed incorrectly.");
		}
	}

	ProjectInfo projectInfo;
	projectInfo.OriginPath      = repoRoot;   // SDK/Templates 해석 기준
	projectInfo.RootPath        = root;
	projectInfo.ProjectFilePath = root / "Probe.jproject";
	projectInfo.ContentPath     = contentPath;
	projectInfo.AssetPath       = root / "Assets";
	projectInfo.ScriptPath      = scriptPath;
	std::filesystem::create_directories(projectInfo.AssetPath, errorCode);
	errorCode.clear();

	const CGameScriptProjectGenerator generator;
	if (false == generator.EnsureProject(projectInfo))
	{
		return Fail(5, "EnsureProject reported a failure.");
	}

	std::string generated;
	if (false == ReadText(contentPath / "GeneratedScriptRegistry.cpp", generated))
	{
		return Fail(6, "GeneratedScriptRegistry.cpp was not produced.");
	}

	const auto contains = [&generated](const char* needle)
	{
		return generated.find(needle) != std::string::npos;
	};

	// ── enum 이름표 ─────────────────────────────────────────────────────────
	if (false == contains("JBroEnum_ECodegenMode_Names")
		|| false == contains("\"Charging\""))
	{
		return Fail(7, "the enum name table is missing from the generated registry.");
	}
	// 값은 enumerator 를 그대로 적어 컴파일러가 계산해야 한다 — 숫자를 파서가 옮겨 적으면
	// `= 1 << 3` 같은 식에서 조용히 틀린다.
	if (false == contains("static_cast<std::int64_t>(ECodegenMode::Charging)"))
	{
		return Fail(8, "enum values are not emitted as enumerator expressions.");
	}
	if (false == contains("JBroEnum_ECodegenMode_Meta"))
	{
		return Fail(9, "the EnumTypeMeta instance is missing.");
	}
	if (false == contains(".Enum = &JBroEnum_ECodegenMode_Meta"))
	{
		return Fail(10, "the enum property does not reference its meta.");
	}
	if (false == contains("EReflectPropertyType::Enum"))
	{
		return Fail(11, "the enum property was not registered as Enum.");
	}

	// ── 이번에 새로 열어 준 스칼라 타입들 ───────────────────────────────────
	if (false == contains("EReflectPropertyType::ColorFloat4"))
	{
		return Fail(12, "a Color property did not map to ColorFloat4.");
	}
	if (false == contains("EReflectPropertyType::Layout2D"))
	{
		return Fail(13, "a Layout2D property did not map to Layout2D.");
	}
	if (false == contains("EReflectPropertyType::Int32"))
	{
		return Fail(14, "an Int32 property did not map to Int32.");
	}
	if (false == contains("EReflectPropertyType::Int64"))
	{
		return Fail(15, "an Int64 property did not map to Int64.");
	}

	// 다섯 프로퍼티가 전부 등록됐는지 — 하나라도 미지원으로 걸러지면 조용히 사라진다.
	for (const char* name : { "Mode", "Tint", "Anchor", "Hits", "Score" })
	{
		if (false == contains((std::string(".Name = \"") + name + "\"").c_str()))
		{
			return Fail(16, std::string("property '") + name + "' was dropped by the generator.");
		}
	}

	if (keepOutput)
	{
		std::cout << "Generated output kept at: " << root.string() << "\n";
	}
	else
	{
		std::filesystem::remove_all(root, errorCode);
	}

	std::cout << "Script codegen smoke test passed.\n";
	return 0;
}
