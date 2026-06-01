#pragma once

#include "Utillity/Math/SizeT.h"
#include "Utillity/Math/Vector2T.h"

#include <algorithm>
#include <utility>

template<typename T>
struct RectT
{
	T Left;
	T Top;
	T Right;
	T Bottom;

	RectT() : Left(0), Top(0), Right(0), Bottom(0) {}
	RectT(T left, T top, T right, T bottom) : Left(left), Top(top), Right(right), Bottom(bottom) {}
	RectT(const Vector2T<T>& position, const Size<T>& size)
		: Left(position.x), Top(position.y), Right(position.x + size.Width), Bottom(position.y + size.Height) {}

	RectT operator+() const { return *this; }
	RectT operator-() const { return RectT(-Left, -Top, -Right, -Bottom); }

	RectT operator+(const RectT& rhs) const { return RectT(Left + rhs.Left, Top + rhs.Top, Right + rhs.Right, Bottom + rhs.Bottom); }
	RectT operator-(const RectT& rhs) const { return RectT(Left - rhs.Left, Top - rhs.Top, Right - rhs.Right, Bottom - rhs.Bottom); }
	RectT operator*(const RectT& rhs) const { return RectT(Left * rhs.Left, Top * rhs.Top, Right * rhs.Right, Bottom * rhs.Bottom); }
	RectT operator/(const RectT& rhs) const { return RectT(Left / rhs.Left, Top / rhs.Top, Right / rhs.Right, Bottom / rhs.Bottom); }

	RectT operator*(T scalar) const { return RectT(Left * scalar, Top * scalar, Right * scalar, Bottom * scalar); }
	RectT operator/(T scalar) const { return RectT(Left / scalar, Top / scalar, Right / scalar, Bottom / scalar); }

	RectT operator+(const Vector2T<T>& offset) const { return RectT(Left + offset.x, Top + offset.y, Right + offset.x, Bottom + offset.y); }
	RectT operator-(const Vector2T<T>& offset) const { return RectT(Left - offset.x, Top - offset.y, Right - offset.x, Bottom - offset.y); }

	RectT& operator+=(const RectT& rhs)
	{
		Left += rhs.Left;
		Top += rhs.Top;
		Right += rhs.Right;
		Bottom += rhs.Bottom;
		return *this;
	}

	RectT& operator-=(const RectT& rhs)
	{
		Left -= rhs.Left;
		Top -= rhs.Top;
		Right -= rhs.Right;
		Bottom -= rhs.Bottom;
		return *this;
	}

	RectT& operator*=(const RectT& rhs)
	{
		Left *= rhs.Left;
		Top *= rhs.Top;
		Right *= rhs.Right;
		Bottom *= rhs.Bottom;
		return *this;
	}

	RectT& operator/=(const RectT& rhs)
	{
		Left /= rhs.Left;
		Top /= rhs.Top;
		Right /= rhs.Right;
		Bottom /= rhs.Bottom;
		return *this;
	}

	RectT& operator*=(T scalar)
	{
		Left *= scalar;
		Top *= scalar;
		Right *= scalar;
		Bottom *= scalar;
		return *this;
	}

	RectT& operator/=(T scalar)
	{
		Left /= scalar;
		Top /= scalar;
		Right /= scalar;
		Bottom /= scalar;
		return *this;
	}

	RectT& operator+=(const Vector2T<T>& offset)
	{
		Left += offset.x;
		Top += offset.y;
		Right += offset.x;
		Bottom += offset.y;
		return *this;
	}

	RectT& operator-=(const Vector2T<T>& offset)
	{
		Left -= offset.x;
		Top -= offset.y;
		Right -= offset.x;
		Bottom -= offset.y;
		return *this;
	}

	bool operator==(const RectT& rhs) const { return Left == rhs.Left && Top == rhs.Top && Right == rhs.Right && Bottom == rhs.Bottom; }
	bool operator!=(const RectT& rhs) const { return !(*this == rhs); }
	bool operator<(const RectT& rhs) const
	{
		if (Left < rhs.Left) return true;
		if (Left > rhs.Left) return false;
		if (Top < rhs.Top) return true;
		if (Top > rhs.Top) return false;
		if (Right < rhs.Right) return true;
		if (Right > rhs.Right) return false;
		return Bottom < rhs.Bottom;
	}
	bool operator>(const RectT& rhs) const { return rhs < *this; }
	bool operator<=(const RectT& rhs) const { return !(*this > rhs); }
	bool operator>=(const RectT& rhs) const { return !(*this < rhs); }

	T GetWidth() const { return Right - Left; }
	T GetHeight() const { return Bottom - Top; }
	T GetArea() const { return GetWidth() * GetHeight(); }
	Size<T> GetSize() const { return Size<T>(GetWidth(), GetHeight()); }
	Vector2T<T> GetTL() const { return Vector2T<T>(Left, Top); }
	Vector2T<T> GetBR() const { return Vector2T<T>(Right, Bottom); }
	Vector2T<T> GetCenter() const { return Vector2T<T>((Left + Right) / static_cast<T>(2), (Top + Bottom) / static_cast<T>(2)); }
	T GetPerimeter() const { return (GetWidth() + GetHeight()) * static_cast<T>(2); }

	float AspectRatio() const
	{
		const T height = GetHeight();
		if (height == 0)
		{
			return 0.0f;
		}

		return static_cast<float>(GetWidth()) / static_cast<float>(height);
	}

	bool IsEmpty() const { return GetWidth() <= 0 || GetHeight() <= 0; }
	bool IsValid() const { return Left <= Right && Top <= Bottom; }

	bool Contains(T x, T y) const
	{
		return x >= Left && x <= Right && y >= Top && y <= Bottom;
	}

	bool Contains(const Vector2T<T>& point) const
	{
		return Contains(point.x, point.y);
	}

	bool Contains(const RectT& rhs) const
	{
		return rhs.Left >= Left && rhs.Top >= Top && rhs.Right <= Right && rhs.Bottom <= Bottom;
	}

	bool Intersects(const RectT& rhs) const
	{
		return !(rhs.Right < Left || rhs.Left > Right || rhs.Bottom < Top || rhs.Top > Bottom);
	}

	RectT Intersection(const RectT& rhs) const
	{
		RectT result(
			std::max(Left, rhs.Left),
			std::max(Top, rhs.Top),
			std::min(Right, rhs.Right),
			std::min(Bottom, rhs.Bottom));

		if (!result.IsValid())
		{
			return RectT();
		}

		return result;
	}

	RectT Union(const RectT& rhs) const
	{
		return RectT(
			std::min(Left, rhs.Left),
			std::min(Top, rhs.Top),
			std::max(Right, rhs.Right),
			std::max(Bottom, rhs.Bottom));
	}

	RectT Inflated(T amount) const { return Inflated(amount, amount); }
	RectT Inflated(T horizontal, T vertical) const
	{
		return RectT(Left - horizontal, Top - vertical, Right + horizontal, Bottom + vertical);
	}

	RectT Deflated(T amount) const { return Deflated(amount, amount); }
	RectT Deflated(T horizontal, T vertical) const
	{
		return RectT(Left + horizontal, Top + vertical, Right - horizontal, Bottom - vertical);
	}

	RectT Offsetted(T dx, T dy) const
	{
		return RectT(Left + dx, Top + dy, Right + dx, Bottom + dy);
	}

	RectT Offsetted(const Vector2T<T>& delta) const
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

	void Offset(const Vector2T<T>& delta)
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
		{
			std::swap(Left, Right);
		}

		if (Top > Bottom)
		{
			std::swap(Top, Bottom);
		}
	}

	RectT Normalized() const
	{
		RectT result(*this);
		result.Normalize();
		return result;
	}

	static RectT FromLTWH(T left, T top, T width, T height)
	{
		return RectT(left, top, left + width, top + height);
	}

	static RectT FromCenter(const Vector2T<T>& center, const Size<T>& size)
	{
		T halfW = size.Width / static_cast<T>(2);
		T halfH = size.Height / static_cast<T>(2);
		return RectT(center.x - halfW, center.y - halfH, center.x + halfW, center.y + halfH);
	}

	static RectT Lerp(const RectT& a, const RectT& b, float t)
	{
		return RectT(
			static_cast<T>(a.Left + (b.Left - a.Left) * t),
			static_cast<T>(a.Top + (b.Top - a.Top) * t),
			static_cast<T>(a.Right + (b.Right - a.Right) * t),
			static_cast<T>(a.Bottom + (b.Bottom - a.Bottom) * t));
	}
};

using Rect = RectT<float>;
