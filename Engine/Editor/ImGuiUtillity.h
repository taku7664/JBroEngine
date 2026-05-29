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

		
	}
}
