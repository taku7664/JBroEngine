#pragma once

template<typename T>
struct Size
{
	T Width;
	T Height;

	Size() : Width(0), Height(0) {}
	Size(T width, T height) : Width(width), Height(height) {}

	Size operator+() const { return *this; }
	Size operator-() const { return Size(-Width, -Height); }

	Size operator+(const Size& rhs) const { return Size(Width + rhs.Width, Height + rhs.Height); }
	Size operator-(const Size& rhs) const { return Size(Width - rhs.Width, Height - rhs.Height); }
	Size operator*(const Size& rhs) const { return Size(Width * rhs.Width, Height * rhs.Height); }
	Size operator/(const Size& rhs) const { return Size(Width / rhs.Width, Height / rhs.Height); }

	Size operator*(T scalar) const { return Size(Width * scalar, Height * scalar); }
	Size operator/(T scalar) const { return Size(Width / scalar, Height / scalar); }

	Size& operator+=(const Size& rhs)
	{
		Width += rhs.Width;
		Height += rhs.Height;
		return *this;
	}

	Size& operator-=(const Size& rhs)
	{
		Width -= rhs.Width;
		Height -= rhs.Height;
		return *this;
	}

	Size& operator*=(const Size& rhs)
	{
		Width *= rhs.Width;
		Height *= rhs.Height;
		return *this;
	}

	Size& operator/=(const Size& rhs)
	{
		Width /= rhs.Width;
		Height /= rhs.Height;
		return *this;
	}

	Size& operator*=(T scalar)
	{
		Width *= scalar;
		Height *= scalar;
		return *this;
	}

	Size& operator/=(T scalar)
	{
		Width /= scalar;
		Height /= scalar;
		return *this;
	}

	bool operator==(const Size& rhs) const { return Width == rhs.Width && Height == rhs.Height; }
	bool operator!=(const Size& rhs) const { return !(*this == rhs); }
	bool operator<(const Size& rhs) const
	{
		if (Width < rhs.Width) return true;
		if (Width > rhs.Width) return false;
		return Height < rhs.Height;
	}
	bool operator>(const Size& rhs) const { return rhs < *this; }
	bool operator<=(const Size& rhs) const { return !(*this > rhs); }
	bool operator>=(const Size& rhs) const { return !(*this < rhs); }

	T Area() const { return Width * Height; }
	bool IsEmpty() const { return Width <= 0 || Height <= 0; }
	float AspectRatio() const
	{
		if (Height == 0)
			return 0.0;

		return static_cast<float>(Width) / static_cast<float>(Height);
	}

	T MinSide() const { return min(Width, Height); }
	T MaxSide() const { return max(Width, Height); }

	static Size Min(const Size& a, const Size& b)
	{
		return Size(std::min(a.Width, b.Width), std::min(a.Height, b.Height));
	}

	static Size Max(const Size& a, const Size& b)
	{
		return Size(std::max(a.Width, b.Width), std::max(a.Height, b.Height));
	}

	static Size Clamp(const Size& value, const Size& minValue, const Size& maxValue)
	{
		return Max(minValue, Min(value, maxValue));
	}


};