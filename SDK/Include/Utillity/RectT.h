#pragma once

template<typename T>
struct Rect
{
	T Left;
	T Top;
	T Right;
	T Bottom;

	Rect() : Left(0), Top(0), Right(0), Bottom(0) {}
	Rect(T left, T top, T right, T bottom) : Left(left), Top(top), Right(right), Bottom(bottom) {}
	Rect(const Vector2<T>& position, const Size<T>& size)
		: Left(position.x), Top(position.y), Right(position.x + size.Width), Bottom(position.y + size.Height) {}

	Rect operator+() const { return *this; }
	Rect operator-() const { return Rect(-Left, -Top, -Right, -Bottom); }

	Rect operator+(const Rect& rhs) const { return Rect(Left + rhs.Left, Top + rhs.Top, Right + rhs.Right, Bottom + rhs.Bottom); }
	Rect operator-(const Rect& rhs) const { return Rect(Left - rhs.Left, Top - rhs.Top, Right - rhs.Right, Bottom - rhs.Bottom); }
	Rect operator*(const Rect& rhs) const { return Rect(Left * rhs.Left, Top * rhs.Top, Right * rhs.Right, Bottom * rhs.Bottom); }
	Rect operator/(const Rect& rhs) const { return Rect(Left / rhs.Left, Top / rhs.Top, Right / rhs.Right, Bottom / rhs.Bottom); }

	Rect operator*(T scalar) const { return Rect(Left * scalar, Top * scalar, Right * scalar, Bottom * scalar); }
	Rect operator/(T scalar) const { return Rect(Left / scalar, Top / scalar, Right / scalar, Bottom / scalar); }

	Rect operator+(const Vector2<T>& offset) const { return Rect(Left + offset.x, Top + offset.y, Right + offset.x, Bottom + offset.y); }
	Rect operator-(const Vector2<T>& offset) const { return Rect(Left - offset.x, Top - offset.y, Right - offset.x, Bottom - offset.y); }

	Rect& operator+=(const Rect& rhs)
	{
		Left += rhs.Left;
		Top += rhs.Top;
		Right += rhs.Right;
		Bottom += rhs.Bottom;
		return *this;
	}

	Rect& operator-=(const Rect& rhs)
	{
		Left -= rhs.Left;
		Top -= rhs.Top;
		Right -= rhs.Right;
		Bottom -= rhs.Bottom;
		return *this;
	}

	Rect& operator*=(const Rect& rhs)
	{
		Left *= rhs.Left;
		Top *= rhs.Top;
		Right *= rhs.Right;
		Bottom *= rhs.Bottom;
		return *this;
	}

	Rect& operator/=(const Rect& rhs)
	{
		Left /= rhs.Left;
		Top /= rhs.Top;
		Right /= rhs.Right;
		Bottom /= rhs.Bottom;
		return *this;
	}

	Rect& operator*=(T scalar)
	{
		Left *= scalar;
		Top *= scalar;
		Right *= scalar;
		Bottom *= scalar;
		return *this;
	}

	Rect& operator/=(T scalar)
	{
		Left /= scalar;
		Top /= scalar;
		Right /= scalar;
		Bottom /= scalar;
		return *this;
	}

	Rect& operator+=(const Vector2<T>& offset)
	{
		Left += offset.x;
		Top += offset.y;
		Right += offset.x;
		Bottom += offset.y;
		return *this;
	}

	Rect& operator-=(const Vector2<T>& offset)
	{
		Left -= offset.x;
		Top -= offset.y;
		Right -= offset.x;
		Bottom -= offset.y;
		return *this;
	}

	bool operator==(const Rect& rhs) const { return Left == rhs.Left && Top == rhs.Top && Right == rhs.Right && Bottom == rhs.Bottom; }
	bool operator!=(const Rect& rhs) const { return !(*this == rhs); }
	bool operator<(const Rect& rhs) const
	{
		if (Left < rhs.Left) return true;
		if (Left > rhs.Left) return false;
		if (Top < rhs.Top) return true;
		if (Top > rhs.Top) return false;
		if (Right < rhs.Right) return true;
		if (Right > rhs.Right) return false;
		return Bottom < rhs.Bottom;
	}
	bool operator>(const Rect& rhs) const { return rhs < *this; }
	bool operator<=(const Rect& rhs) const { return !(*this > rhs); }
	bool operator>=(const Rect& rhs) const { return !(*this < rhs); }

	T			GetWidth()		const { return Right - Left; }
	T			GetHeight()		const { return Bottom - Top; }
	T			GetArea()		const { return GetWidth() * GetHeight; }
	Size<T>		GetSize()		const { return Size<T>(GetWidth(), GetHeight); }
	Vector2<T>	GetTL()			const { return Vector2<T>(Left, Top); }
	Vector2<T>	GetBR()			const { return Vector2<T>(Right, Bottom); }
	Vector2<T>	GetCenter()		const { return Vector2<T>((Left + Right) / static_cast<T>(2), (Top + Bottom) / static_cast<T>(2)); }
	T			GetPerimeter()	const { return (GetWidth() + GetHeight) * static_cast<T>(2); }

	float AspectRatio() const
	{
		T h = GetHeight;
		if (h == 0)
			return 0.0f;

		return static_cast<float>(GetWidth()) / static_cast<float>(h);
	}

	bool IsEmpty() const { return GetWidth() <= 0 || GetHeight <= 0; }
	bool IsValid() const { return Left <= Right && Top <= Bottom; }

	bool Contains(T x, T y) const
	{
		return x >= Left && x <= Right && y >= Top && y <= Bottom;
	}

	bool Contains(const Vector2<T>& point) const
	{
		return Contains(point.x, point.y);
	}

	bool Contains(const Rect& rhs) const
	{
		return rhs.Left >= Left && rhs.Top >= Top && rhs.Right <= Right && rhs.Bottom <= Bottom;
	}

	bool Intersects(const Rect& rhs) const
	{
		return !(rhs.Right < Left || rhs.Left > Right || rhs.Bottom < Top || rhs.Top > Bottom);
	}

	Rect Intersection(const Rect& rhs) const
	{
		Rect result(
			std::max(Left, rhs.Left),
			std::max(Top, rhs.Top),
			std::min(Right, rhs.Right),
			std::min(Bottom, rhs.Bottom));

		if (!result.IsValid())
			return Rect();

		return result;
	}

	Rect Union(const Rect& rhs) const
	{
		return Rect(
			std::min(Left, rhs.Left),
			std::min(Top, rhs.Top),
			std::max(Right, rhs.Right),
			std::max(Bottom, rhs.Bottom));
	}

	Rect Inflated(T amount) const { return Inflated(amount, amount); }
	Rect Inflated(T horizontal, T vertical) const
	{
		return Rect(Left - horizontal, Top - vertical, Right + horizontal, Bottom + vertical);
	}

	Rect Deflated(T amount) const { return Deflated(amount, amount); }
	Rect Deflated(T horizontal, T vertical) const
	{
		return Rect(Left + horizontal, Top + vertical, Right - horizontal, Bottom - vertical);
	}

	Rect Offsetted(T dx, T dy) const
	{
		return Rect(Left + dx, Top + dy, Right + dx, Bottom + dy);
	}

	Rect Offsetted(const Vector2<T>& delta) const
	{
		return Offsetted(delta.x, delta.y);
	}

	void Offset(T dx, T dy)
	{
		Left += dx;
		Top += dy;
		Right += dx;
		Bottom += dy;
	}

	void Offset(const Vector2<T>& delta)
	{
		Offset(delta.x, delta.y);
	}

	void Inflate(T amount) { Inflate(amount, amount); }
	void Inflate(T horizontal, T vertical)
	{
		Left -= horizontal;
		Top -= vertical;
		Right += horizontal;
		Bottom += vertical;
	}

	void Deflate(T amount) { Deflate(amount, amount); }
	void Deflate(T horizontal, T vertical)
	{
		Left += horizontal;
		Top += vertical;
		Right -= horizontal;
		Bottom -= vertical;
	}

	void Normalize()
	{
		if (Left > Right)
			std::swap(Left, Right);

		if (Top > Bottom)
			std::swap(Top, Bottom);
	}

	Rect Normalized() const
	{
		Rect result(*this);
		result.Normalize();
		return result;
	}

	static Rect FromLTWH(T left, T top, T width, T height)
	{
		return Rect(left, top, left + width, top + height);
	}

	static Rect FromCenter(const Vector2<T>& center, const Size<T>& size)
	{
		T halfW = size.Width / static_cast<T>(2);
		T halfH = size.Height / static_cast<T>(2);
		return Rect(center.x - halfW, center.y - halfH, center.x + halfW, center.y + halfH);
	}

	static Rect Lerp(const Rect& a, const Rect& b, float t)
	{
		return Rect(
			static_cast<T>(a.Left + (b.Left - a.Left) * t),
			static_cast<T>(a.Top + (b.Top - a.Top) * t),
			static_cast<T>(a.Right + (b.Right - a.Right) * t),
			static_cast<T>(a.Bottom + (b.Bottom - a.Bottom) * t));
	}


};