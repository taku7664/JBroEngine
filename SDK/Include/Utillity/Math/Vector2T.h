#pragma once

#include <cmath>

template<typename T>
struct Vector2T
{
	T x;
	T y;

	Vector2T() : x(0), y(0) {}
	Vector2T(T xValue, T yValue) : x(xValue), y(yValue) {}

	Vector2T operator+() const { return *this; }
	Vector2T operator-() const { return Vector2T(-x, -y); }

	Vector2T operator+(const Vector2T& rhs) const { return Vector2T(x + rhs.x, y + rhs.y); }
	Vector2T operator-(const Vector2T& rhs) const { return Vector2T(x - rhs.x, y - rhs.y); }
	Vector2T operator*(const Vector2T& rhs) const { return Vector2T(x * rhs.x, y * rhs.y); }
	Vector2T operator/(const Vector2T& rhs) const { return Vector2T(x / rhs.x, y / rhs.y); }

	Vector2T operator*(T scalar) const { return Vector2T(x * scalar, y * scalar); }
	Vector2T operator/(T scalar) const { return Vector2T(x / scalar, y / scalar); }

	Vector2T& operator+=(const Vector2T& rhs)
	{
		x += rhs.x;
		y += rhs.y;
		return *this;
	}

	Vector2T& operator-=(const Vector2T& rhs)
	{
		x -= rhs.x;
		y -= rhs.y;
		return *this;
	}

	Vector2T& operator*=(const Vector2T& rhs)
	{
		x *= rhs.x;
		y *= rhs.y;
		return *this;
	}

	Vector2T& operator/=(const Vector2T& rhs)
	{
		x /= rhs.x;
		y /= rhs.y;
		return *this;
	}

	Vector2T& operator*=(T scalar)
	{
		x *= scalar;
		y *= scalar;
		return *this;
	}

	Vector2T& operator/=(T scalar)
	{
		x /= scalar;
		y /= scalar;
		return *this;
	}

	bool operator==(const Vector2T& rhs) const { return x == rhs.x && y == rhs.y; }
	bool operator!=(const Vector2T& rhs) const { return !(*this == rhs); }
	bool operator<(const Vector2T& rhs) const
	{
		if (x < rhs.x) return true;
		if (x > rhs.x) return false;
		return y < rhs.y;
	}
	bool operator>(const Vector2T& rhs) const { return rhs < *this; }
	bool operator<=(const Vector2T& rhs) const { return !(*this > rhs); }
	bool operator>=(const Vector2T& rhs) const { return !(*this < rhs); }

	T LengthSqrt() const { return x * x + y * y; }
	float Length() const { return std::sqrt(static_cast<float>(LengthSqrt())); }

	T Dot(const Vector2T& rhs) const { return x * rhs.x + y * rhs.y; }
	T Cross(const Vector2T& rhs) const { return x * rhs.y - y * rhs.x; }

	void Normalize()
	{
		float len = Length();
		if (len == 0.0)
			return;

		x = static_cast<T>(x / len);
		y = static_cast<T>(y / len);
	}

	Vector2T Normalized() const
	{
		Vector2T result(*this);
		result.Normalize();
		return result;
	}

	static T DistanceSq(const Vector2T& a, const Vector2T& b)
	{
		return (a - b).LengthSqrt();
	}

	static double Distance(const Vector2T& a, const Vector2T& b)
	{
		return (a - b).Length();
	}

	static Vector2T Lerp(const Vector2T& a, const Vector2T& b, T t)
	{
		return a + (b - a) * t;
	}

};

template<typename T>
Vector2T<T> operator*(T scalar, const Vector2T<T>& vec)
{
	return vec * scalar;
}

using Vector2 = Vector2T<float>;
