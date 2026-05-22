#include "pch.h"
#include "ImText.h"

ImText::ImText(const char* text)
	: m_scale(1.0f)
	, m_align(Align::Left)
{
	if (text)
	{
		operator()(text);
	}
}

ImText::ImText(const char* text, Align align)
	: m_scale(1.0f)
	, m_align(align)
{
	if (text)
	{
		operator()(text);
	}
}

ImText::ImText(const char* text, float scale, Align align)
	: m_scale(scale)
	, m_align(align)
{
	if (text)
	{
		operator()(text);
	}
}

ImText::ImText( Align align )
	: m_scale(1.0f)
	, m_align(align)
{
}

ImText::ImText( float scale , Align align )
	: m_scale( scale )
	, m_align( align )
{
}

ImText::~ImText()
{
}

void ImText::operator()( const char* text )
{
	operator()( text , m_scale , m_align );
}

void ImText::operator()( const char* text , Align align )
{
	operator()( text , m_scale , align );
}

void ImText::operator()( const char* text , float scale , Align align )
{
	if ( scale > 0 )
	{
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		float old = window->FontWindowScale;
		ImGui::SetWindowFontScale( scale );

		float windowWidth = ImGui::GetWindowSize().x;
		float textWidth = ImGui::CalcTextSize( text ).x;
		float weight = 0.0f;
		ImVec2 spacing = ImVec2( 0 , 0 );

		switch ( align )
		{
		case Align::Left:
			weight = 0.0f;
			spacing = ImGui::GetStyle().ItemSpacing;
			break;
		case Align::Right:
			weight = 1.0f;
			spacing = -ImGui::GetStyle().ItemSpacing;
			break;
		case Align::Center:
			weight = 0.5f;
			break;
		default:
			break;
		}

		ImGui::SetCursorPosX( spacing.x + ( windowWidth - textWidth ) * weight );
		ImGui::Text( "%s" , text );

		ImGui::SetWindowFontScale( old );
	}
}

