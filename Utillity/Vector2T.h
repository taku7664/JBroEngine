#pragma once

template<typename T>
struct Vector2
{
	T x;
	T y;

	Vector2() : x(0), y(0) {}
	Vector2(T xValue, T yValue) : x(xValue), y(yValue) {}

	Vector2 operator+() const { return *this; }
	Vector2 operator-() const { return Vector2(-x, -y); }

	Vector2 operator+(const Vector2& rhs) const { return Vector2(x + rhs.x, y + rhs.y); }
	Vector2 operator-(const Vector2& rhs) const { return Vector2(x - rhs.x, y - rhs.y); }
	Vector2 operator*(const Vector2& rhs) const { return Vector2(x * rhs.x, y * rhs.y); }
	Vector2 operator/(const Vector2& rhs) const { return Vector2(x / rhs.x, y / rhs.y); }

	Vector2 operator*(T scalar) const { return Vector2(x * scalar, y * scalar); }
	Vector2 operator/(T scalar) const { return Vector2(x / scalar, y / scalar); }

	Vector2& operator+=(const Vector2& rhs)
	{
		x += rhs.x;
		y += rhs.y;
		return *this;
	}

	Vector2& operator-=(const Vector2& rhs)
	{
		x -= rhs.x;
		y -= rhs.y;
		return *this;
	}

	Vector2& operator*=(const Vector2& rhs)
	{
		x *= rhs.x;
		y *= rhs.y;
		return *this;
	}

	Vector2& operator/=(const Vector2& rhs)
	{
		x /= rhs.x;
		y /= rhs.y;
		return *this;
	}

	Vector2& operator*=(T scalar)
	{
		x *= scalar;
		y *= scalar;
		return *this;
	}

	Vector2& operator/=(T scalar)
	{
		x /= scalar;
		y /= scalar;
		return *this;
	}

	bool operator==(const Vector2& rhs) const { return x == rhs.x && y == rhs.y; }
	bool operator!=(const Vector2& rhs) const { return !(*this == rhs); }
	bool operator<(const Vector2& rhs) const
	{
		if (x < rhs.x) return true;
		if (x > rhs.x) return false;
		return y < rhs.y;
	}
	bool operator>(const Vector2& rhs) const { return rhs < *this; }
	bool operator<=(const Vector2& rhs) const { return !(*this > rhs); }
	bool operator>=(const Vector2& rhs) const { return !(*this < rhs); }

	T LengthSqrt() const { return x * x + y * y; }
	float Length() const { return std::sqrt(static_cast<float>(LengthSqrt())); }

	T Dot(const Vector2& rhs) const { return x * rhs.x + y * rhs.y; }
	T Cross(const Vector2& rhs) const { return x * rhs.y - y * rhs.x; }

	void Normalize()
	{
		float len = Length();
		if (len == 0.0)
			return;

		x = static_cast<T>(x / len);
		y = static_cast<T>(y / len);
	}

	Vector2 Normalized() const
	{
		Vector2 result(*this);
		result.Normalize();
		return result;
	}

	static T DistanceSq(const Vector2& a, const Vector2& b)
	{
		return (a - b).LengthSqrt();
	}

	static double Distance(const Vector2& a, const Vector2& b)
	{
		return (a - b).LengthSqrt();
	}

	static Vector2 Lerp(const Vector2& a, const Vector2& b, T t)
	{
		return a + (b - a) * t;
	}

};

template<typename T>
Vector2<T> operator*(T scalar, const Vector2<T>& vec)
{
	return vec * scalar;
}