#pragma once

class ImText
{
public:
	enum class Align
	{
		Left,
		Center,
		Right,
	};
public:
	ImText(const char* text);
	ImText(const char* text, Align align);
	ImText(const char* text, float scale, Align align = Align::Left);
	ImText(Align align = Align::Left);
	ImText(float scale, Align align = Align::Left);
	~ImText();
	void operator()( const char* text );
	void operator()( const char* text , Align align );
	void operator()( const char* text , float scale , Align align = Align::Left );

private:
	float m_scale;
	Align m_align;
};

