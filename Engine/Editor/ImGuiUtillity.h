#pragma once

#include <limits>
#include <string>
#include <type_traits>

namespace ImGui
{
    namespace Utillity
    {
        bool IsWindowDrawable(ImGuiWindow* window = nullptr);

		void TextWithVerticalSeparator(const char* text, float startX = FLT_MAX);

		bool HoveredToolTip(const char* toolTip, ImGuiHoveredFlags flags = ImGuiHoveredFlags_None);

		enum class CheckMarkType { Check, X, Circle, };
		bool Checkbox(const char* label, bool* v, CheckMarkType checkType = CheckMarkType::Check);

		bool VerticalSplitter(const char* id, float& ratio, ImVec2 availSpace,
			const float minRatio = 0.15f, const float maxRatio = 0.8f,
			float thickness = 1.0f);

		void LoadingSpinner(float radius = 0.0f, ImVec4 color = ImVec4(1, 1, 1, 1));

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

		// ── ValidatedInputText ──────────────────────────────────────────────
		// std::string 버전 InputText 의 얇은 래퍼.  invalid 상태일 때
		// 프레임 경계선을 빨간색으로 표시한다.
		//
		// 사용:
		//   ImGui::Utillity::ValidatedInputText("##name", &name,
		//       /*invalid*/ name.empty());
		//
		// invalidIfEmpty 와 explicit invalid 둘 다 받지 않고, 호출자가
		// invalid 여부를 직접 판단하도록 단순화 — empty 외의 규칙(중복 이름,
		// 정규식 등)도 같은 함수로 자연스럽게 표시 가능.
		inline bool ValidatedInputText(const char* id,
		                               std::string* buffer,
		                               bool invalid,
		                               ImGuiInputTextFlags flags = 0)
		{
			if (invalid)
			{
				const ImVec4 red(0.85f, 0.20f, 0.20f, 1.0f);
				ImGui::PushStyleColor(ImGuiCol_Border, red);
				ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);
			}
			const bool changed = ImGui::InputText(id, buffer, flags);
			if (invalid)
			{
				ImGui::PopStyleVar();
				ImGui::PopStyleColor();
			}
			return changed;
		}

		// ── List ────────────────────────────────────────────────────────────
		// 마지막 하단 + / - 버튼.  좌측의 핸들(=) 을 잡아 다른 원소 사이의
		// 슬롯에 드롭하면 그 위치로 이동한다.
		// 반환: true 면 원소 추가/삭제/재정렬로 상태가 변했음.
		// 헤더 전용 템플릿이라 별도 cpp 없이 사용 가능.
		template <typename T, typename TDrawRowFunc>
		bool List(const char* id, std::vector<T>& items,
		          TDrawRowFunc&& drawRow, T defaultValue = T{})
		{
			bool changed = false;
			ImGui::PushID(id);

			// 박스 외곽
			const ImGuiChildFlags childFlags =
				ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY;
			ImGui::BeginChild("##list_body",
				ImVec2(0.0f, 0.0f), childFlags);

			// 드롭 슬롯과 행 사이의 세로 간격을 제거 — 슬롯 자체가 4px 영역을
			// 가지므로 ItemSpacing.y 까지 더해지면 행 사이가 너무 벌어진다.
			const ImVec2 prevSpacing = ImGui::GetStyle().ItemSpacing;
			ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(prevSpacing.x, 0.0f));

			constexpr float ROW_HANDLE_W   = 14.0f;
			constexpr float ROW_REMOVE_W   = 22.0f;
			constexpr float CONTROL_BTN_W  = 22.0f;
			constexpr float SLOT_HEIGHT    = 4.0f;
			// 드래그 페이로드 식별자 — 같은 List 인스턴스 안에서만 유효하게
			// 호출자의 id 를 함께 사용한다.
			constexpr const char* DRAG_PAYLOAD = "JBRO_LIST_REORDER";

			int removeIdx = -1;
			int moveFrom  = -1;
			int moveTo    = -1;

			auto drawDropSlot = [&](int slotIndex)
			{
				// 사이에 얇은 invisible 영역 — drag drop target 으로 사용.
				const ImVec2 cursor = ImGui::GetCursorScreenPos();
				const float  availW = ImGui::GetContentRegionAvail().x;
				ImGui::PushID(slotIndex);
				ImGui::InvisibleButton("##slot", ImVec2(availW, SLOT_HEIGHT));
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload(DRAG_PAYLOAD))
					{
						moveFrom = *static_cast<const int*>(p->Data);
						moveTo   = slotIndex;
					}
					// hover 시 시각적 가이드 라인
					ImGui::GetWindowDrawList()->AddLine(
						ImVec2(cursor.x, cursor.y + SLOT_HEIGHT * 0.5f),
						ImVec2(cursor.x + availW, cursor.y + SLOT_HEIGHT * 0.5f),
						ImGui::GetColorU32(ImGuiCol_DragDropTarget), 2.0f);
					ImGui::EndDragDropTarget();
				}
				ImGui::PopID();
			};

			for (int i = 0; i < static_cast<int>(items.size()); ++i)
			{
				drawDropSlot(i);

				ImGui::PushID(i);
				const ImVec2	windowPadding = ImGui::GetStyle().WindowPadding;
				const float     frameHeight = ImGui::GetFrameHeight();
				const float		rowAvailW = ImGui::GetContentRegionAvail().x;
				const float		contentW  = rowAvailW - ROW_HANDLE_W - ROW_REMOVE_W - 8.0f;

				// 좌측 핸들 — 드래그 소스. 핸들만 잡아야 콘텐츠의 일반 InputText 와
				// 충돌하지 않는다.
				const char* selectableLabel = "= ";
				const ImVec2 availSpace = ImGui::GetContentRegionAvail();
				ImGui::AlignTextToFramePadding();
				ImGui::Selectable("##row_body", false, ImGuiSelectableFlags_AllowOverlap, ImVec2(availSpace.x, frameHeight));
				{	// DragDrop Start
					ImGui::Utillity::StyleBuilder styleBuilder;
					styleBuilder.PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
					styleBuilder.PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
					styleBuilder.PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
					if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceNoHoldToOpenOthers))
					{
						styleBuilder.PopStyle();
						int srcIdx = i;
						ImGui::SetDragDropPayload(DRAG_PAYLOAD, &srcIdx, sizeof(int));
						{
							ImGui::Utillity::DisableScope disable;
							drawRow(items[i], i);
						}
						ImGui::EndDragDropSource();
					}
				}
				ImGui::SameLine();
				ImGui::TextUnformatted(selectableLabel);
				ImGui::SameLine();

				// 컨텐츠 영역
				ImGui::BeginGroup();
				ImGui::PushItemWidth(contentW);
				drawRow(items[i], i);
				ImGui::PopItemWidth();
				ImGui::EndGroup();

				// 우측 삭제 버튼
				ImGui::SameLine();
				ImGui::SetCursorPosX(ImGui::GetCursorPosX()
					+ ImGui::GetContentRegionAvail().x - ROW_REMOVE_W);
				if (ImGui::SmallButton("x"))
				{
					removeIdx = i;
				}
				ImGui::PopID();
			}
			// 마지막 원소 뒤의 슬롯 (맨 끝으로 이동)
			drawDropSlot(static_cast<int>(items.size()));

			if (removeIdx >= 0)
			{
				items.erase(items.begin() + removeIdx);
				changed = true;
			}
			else if (moveFrom >= 0 && moveTo >= 0
			         && moveFrom != moveTo && moveFrom + 1 != moveTo)
			{
				T tmp = std::move(items[moveFrom]);
				items.erase(items.begin() + moveFrom);
				if (moveFrom < moveTo) --moveTo;
				items.insert(items.begin() + moveTo, std::move(tmp));
				changed = true;
			}

			// 하단 + / - 버튼 직전에 한 줄 분량의 약간의 공간을 두어
			// 마지막 슬롯과 버튼이 너무 붙지 않도록 한다.
			ImGui::PopStyleVar();   // ItemSpacing 원복 — footer 는 정상 spacing 사용

			const float footerW = CONTROL_BTN_W * 2.0f + 4.0f;
			const float availW  = ImGui::GetContentRegionAvail().x;
			if (availW > footerW)
			{
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + availW - footerW);
			}
			if (ImGui::Button("+", ImVec2(CONTROL_BTN_W, 0.0f)))
			{
				items.push_back(defaultValue);
				changed = true;
			}
			ImGui::SameLine();
			ImGui::BeginDisabled(items.empty());
			if (ImGui::Button("-", ImVec2(CONTROL_BTN_W, 0.0f)))
			{
				items.pop_back();
				changed = true;
			}
			ImGui::EndDisabled();

			ImGui::EndChild();
			ImGui::PopID();
			return changed;
		}

		class TextEx
		{
		public:
			void Show(const char* text);

			TextEx& UseHoveredToolTip(bool use = true, ImGuiHoveredFlags flags = ImGuiHoveredFlags_None);

		private:
			bool m_useTooltip = false;
			ImGuiHoveredFlags m_hoveredFlags = ImGuiHoveredFlags_None;
		};
	}
	void RenderXMark( ImDrawList* drawList , ImVec2 min , ImVec2 max , float thickness = 2.0f );
	void RenderCircleMark(ImDrawList* drawList, ImVec2 min, ImVec2 max, float thickness = 2.0f);
}
