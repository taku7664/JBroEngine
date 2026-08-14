#pragma once

#include <limits>
#include <string>
#include <type_traits>
#include <vector>

#include "ThirdParty/imgui/imgui.h"
#include "ThirdParty/imgui/imgui_internal.h"   // ImGuiWindow
#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR
#include "Utillity/File/FileUtillities.h"
#endif

namespace ImGui
{
    namespace Utillity
    {
        bool IsWindowDrawable(ImGuiWindow* window = nullptr);

		bool HoveredToolTip(const char* toolTip, ImGuiHoveredFlags flags = ImGuiHoveredFlags_None);

		// 도킹된 창이 자기 노드에서 앞으로 오도록 탭을 고른다.
		// SetNextWindowFocus 는 nav 포커스만 옮긴다. 탭 선택은 ImGui 가
		// DockNodeUpdateTabBar 의 "Apply NavWindow focus back to the tab bar" 한 곳에서만
		// 되돌려 주는데, 조건이 NavWindow->RootWindow->DockNode == node 라 **한 단계**만 통한다.
		// 그래서 dockspace 가 중첩되면(루트 > 툴 dock > 패널) 안쪽 창을 포커스해도 바깥
		// 탭바는 그 사실을 모른 채 다른 탭을 계속 앞에 둔다.
		//
		// 여기서는 **자기 한 단계만** 처리한다. 조상은 각자 자기 Begin 에서 자기 노드를 처리한다
		// (CImWindow::Focus 가 소유자에게 요청을 전파한다). ImGui 의 노드 그래프를 타고
		// 올라가면 중간 호스트가 숨어 있을 때 HostWindow 가 비어 길이 끊긴다 — 하필
		// "새 탭이라 아직 안 보이는" 상황이 정확히 그 경우다.
		//
		// 반환값: 처리가 끝났으면 true.
		//         이번 프레임에 막 도킹돼 노드/탭이 아직 없으면 false — 호출부가 다음 프레임에 재시도한다.
		bool SelectDockTab(ImGuiWindow* window);

#if JBRO_PLATFORM_WINDOWS && JBRO_EDITOR
		File::FileDialogOwnerHandle GetDialogOwnerHandle(File::FileDialogOwnerHandle owner = nullptr);

		bool BrowseFolderButton(
			const char* id,
			std::string& inOutPath,
			const wchar_t* title,
			const wchar_t* initialDirectory = L"",
			File::FileDialogOwnerHandle owner = nullptr);

		bool BrowseFileButton(
			const char* id,
			std::string& inOutPath,
			const wchar_t* title,
			const wchar_t* initialDirectory,
			std::vector<File::FileDialogFilter> filters,
			File::FileDialogOwnerHandle owner = nullptr);

		bool BrowseFilesButton(
			const char* id,
			std::vector<File::Path>& outPaths,
			const wchar_t* title,
			const wchar_t* initialDirectory,
			std::vector<File::FileDialogFilter> filters,
			File::FileDialogOwnerHandle owner = nullptr);
#endif

		
		void LoadingSpinner(float radius = 0.0f, ImVec4 color = ImVec4(1, 1, 1, 1));
		void LoadingSpinnerEx(float radius, float thickness, float spinSpeed, ImVec4 color);

		// 체크(✓) 아이콘을 그린다. radius <= 0 이면 LoadingSpinner 와 동일하게 프레임
		// 높이 기준으로 자동 계산하므로 두 위젯을 같은 줄에서 자연스럽게 교체할 수 있다.
		// 커서를 LoadingSpinner 와 동일한 크기만큼 전진시킨다(SameLine 으로 이어붙임).
		void CheckMark(float radius = 0.0f, ImVec4 color = ImVec4(1, 1, 1, 1));

		class StyleBuilder
		{
		public:
			StyleBuilder() = default;
			~StyleBuilder();

		public:
			template <typename T>
			void PushStyleVar( int idx , const T& color )
			{
				ImGui::PushStyleVar( idx , color );
				++m_pushStyleVarCount;
			}
			template <typename T>
			void PushStyleColor( int idx , const T& color )
			{
				ImGui::PushStyleColor( idx , color );
				++m_pushStyleColCount;
			}
			void PopStyle();

		private:
			int m_pushStyleVarCount = 0;
			int m_pushStyleColCount = 0;
		};

		class DisableScope
		{
		public:
			DisableScope(bool disable = true);
			~DisableScope();

			inline bool IsDisabled() const { return m_disabled; }

		private:
			bool m_disabled = false;
		};

		// 값이 유효하지 않은 입력 필드에 빨간 외곽선을 두른다. 범위 안에서 그려지는 프레임 위젯
		// 전체에 걸리므로 InputText 뿐 아니라 InputScalar/DragFloat 같은 것에도 쓸 수 있다.
		// invalid 가 false 면 아무것도 하지 않는다(호출부에서 분기하지 않아도 되게).
		class InvalidFieldScope
		{
		public:
			InvalidFieldScope(bool invalid = true);
			~InvalidFieldScope();

			InvalidFieldScope(const InvalidFieldScope&) = delete;
			InvalidFieldScope& operator=(const InvalidFieldScope&) = delete;

		private:
			bool m_invalid = false;
		};

		class FormLayout
		{
		public:
			FormLayout(const char* id, float spacing = 4.0f, ImVec2 padding = ImVec2(2.0f, 1.0f), float labelWidth = 0.0f);
			~FormLayout();

			FormLayout(const FormLayout&) = delete;
			FormLayout& operator=(const FormLayout&) = delete;

			FormLayout(FormLayout&&) = delete;
			FormLayout& operator=(FormLayout&&) = delete;

		public:
			template <typename TLabelFunc, typename TDrawFunc>
			void Row(TLabelFunc&& leftFunc, TDrawFunc&& rightFunc)
			{
				if (!m_isOpen)
				{
					return;
				}

				ImGui::TableNextRow();

				ImGui::TableSetColumnIndex(0);
				ImGui::AlignTextToFramePadding();

				leftFunc();

				ImGui::TableSetColumnIndex(1);

				if (m_spacing > 0.0f)
				{
					ImGui::SetCursorPosX(ImGui::GetCursorPosX() + m_spacing);
				}

				ImGui::SetNextItemWidth(-FLT_MIN);

				rightFunc();
			}

			bool IsOpen() const;

		private:
			bool m_isOpen = false;

			float m_spacing = 0.0f;
			float m_labelWidth = 0.0f;

			StyleBuilder m_styleBuilder;
		};

		class IDGroup
		{
			public:
			IDGroup() = default;
			IDGroup(const char* strId);
			IDGroup(int intId);
			IDGroup(const void* ptrId);
			template<typename T, typename = std::enable_if_t<std::is_integral_v<T> && false == std::is_same_v<std::decay_t<T>, bool>>>
			IDGroup(T intId)
			{
				PushIntegralId(intId);
			}
			~IDGroup();
			IDGroup(const IDGroup&) = delete;
			IDGroup& operator=(const IDGroup&) = delete;
			IDGroup(IDGroup&&) = delete;
			IDGroup& operator=(IDGroup&&) = delete;

		private:
			template<typename T>
			void PushIntegralId(T intId)
			{
				if constexpr (std::is_signed_v<T>)
				{
					const long long value = static_cast<long long>(intId);
					if (value >= static_cast<long long>(std::numeric_limits<int>::min())
						&& value <= static_cast<long long>(std::numeric_limits<int>::max()))
					{
						ImGui::PushID(static_cast<int>(value));
						m_hasId = true;
						return;
					}
				}
				else
				{
					const unsigned long long value = static_cast<unsigned long long>(intId);
					if (value <= static_cast<unsigned long long>(std::numeric_limits<int>::max()))
					{
						ImGui::PushID(static_cast<int>(value));
						m_hasId = true;
						return;
					}
				}

				m_largeNumericId = std::to_string(intId);
				ImGui::PushID(m_largeNumericId.c_str());
				m_hasId = true;
			}

		private:
			bool m_hasId = false;
			std::string m_largeNumericId;
		};

		
	}
}
