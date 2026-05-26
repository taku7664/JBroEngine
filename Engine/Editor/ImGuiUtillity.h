#pragma once

namespace ImGui
{
    namespace Utillity
    {
        /// <summary>
        /// Returns whether the ImGui window can draw items.
        /// </summary>
        bool IsWindowDrawable(ImGuiWindow* window = nullptr);

		/// <summary>
		/// <para>Draws text followed by a vertical separator.</para>
		/// <para>startX can override the separator position.</para>
		/// </summary>
		void TextWithVerticalSeparator(const char* text, float startX = FLT_MAX);

		bool HoveredToolTip(const char* toolTip, ImGuiHoveredFlags flags = ImGuiHoveredFlags_None);

		enum class CheckMarkType { Check, X, Circle, };
		bool Checkbox(const char* label, bool* v, CheckMarkType checkType = CheckMarkType::Check);

		class StyleBuilder
		{
		public:
			StyleBuilder() = default;
			~StyleBuilder() { PopStyle(); }

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
			void PopStyle()
			{
				if ( m_pushStyleVarCount > 0 )
				{
					ImGui::PopStyleVar( m_pushStyleVarCount );
					m_pushStyleVarCount = 0;
				}
				if ( m_pushStyleColCount > 0 )
				{
					ImGui::PopStyleColor( m_pushStyleColCount );
					m_pushStyleColCount = 0;
				}
			}

		private:
			int m_pushStyleVarCount = 0;
			int m_pushStyleColCount = 0;
		};

		class DisableScope
		{
		public:
			DisableScope(bool disable = true) 
				: m_disabled(disable)
			{ 
				if ( m_disabled ) ImGui::BeginDisabled(); 
			}
			~DisableScope() 
			{
				if ( m_disabled ) ImGui::EndDisabled();
			}

			inline bool IsDisabled() const { return m_disabled; }

		private:
			bool m_disabled = false;
		};

		class FormLayout
		{
		public:
			FormLayout(const char* id, float spacing = 4.0f, ImVec2 padding = ImVec2(2.0f, 1.0f), float labelWidth = 0.0f)
				: m_spacing(spacing)
				, m_labelWidth(labelWidth)
			{

				const ImGuiTableFlags tableFlags =
					ImGuiTableFlags_SizingStretchProp |
					ImGuiTableFlags_NoSavedSettings |
					ImGuiTableFlags_NoBordersInBody |
					ImGuiTableFlags_NoPadOuterX;

				m_isOpen = ImGui::BeginTable(id, 2, tableFlags);

				if (m_isOpen)
				{
					m_styleBuilder.PushStyleVar(ImGuiStyleVar_CellPadding, padding);

					const ImGuiTableColumnFlags labelColumnFlags =
						ImGuiTableColumnFlags_WidthFixed;

					const ImGuiTableColumnFlags fieldColumnFlags =
						ImGuiTableColumnFlags_WidthStretch;

					// labelWidth == 0.0f 이면 ImGui가 라벨 컬럼 폭을 자동 계산합니다.
					// labelWidth != 0.0f 이면 지정한 고정 폭을 사용합니다.
					ImGui::TableSetupColumn("Label", labelColumnFlags, m_labelWidth);
					ImGui::TableSetupColumn("Field", fieldColumnFlags);
				}
			}

			~FormLayout()
			{
				if (m_isOpen)
				{
					ImGui::EndTable();
				}
			}

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

			bool IsOpen() const
			{
				return m_isOpen;
			}

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
			IDGroup(const char* strId)
			{
				ImGui::PushID(strId);
			}
			IDGroup(int intId)
			{
				ImGui::PushID(intId);
			}
			IDGroup(const void* ptrId)
			{
				ImGui::PushID(ptrId);
			}
			~IDGroup()
			{
				ImGui::PopID();
			}
			IDGroup(const IDGroup&) = delete;
			IDGroup& operator=(const IDGroup&) = delete;
			IDGroup(IDGroup&&) = delete;
			IDGroup& operator=(IDGroup&&) = delete;
		};

		class TextEx
		{
		public:
			void Show(const char* text)
			{
				ImGui::TextUnformatted(text);
				if (m_useTooltip)
				{
					HoveredToolTip(text, m_hoveredFlags);
				}
			}

			TextEx& UseHoveredToolTip(bool use = true, ImGuiHoveredFlags flags = ImGuiHoveredFlags_None)
			{
				m_useTooltip = use;
				m_hoveredFlags = flags;
				return *this;
			}

		private:
			bool m_useTooltip = false;
			ImGuiHoveredFlags m_hoveredFlags = ImGuiHoveredFlags_None;
		};
    }
	void RenderXMark( ImDrawList* drawList , ImVec2 min , ImVec2 max , float thickness = 2.0f );
	void RenderCircleMark(ImDrawList* drawList, ImVec2 min, ImVec2 max, float thickness = 2.0f);
}
