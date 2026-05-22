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

		bool HoveredToolTip(const char* toolTip, int flags = ImGuiHoveredFlags_None);

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

		ImVec4 ColorFromGuid(const GUID& guid);
    }
	void RenderXMark( ImDrawList* drawList , ImVec2 min , ImVec2 max , float thickness = 2.0f );
	void RenderCircleMark(ImDrawList* drawList, ImVec2 min, ImVec2 max, float thickness = 2.0f);
}
