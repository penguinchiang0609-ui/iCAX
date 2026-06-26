#pragma once
#include "Data.h"
#include <functional>
#include <ostream>
#include <type_traits>
#include <vector>

namespace iCAX
{
    namespace Data
    {
        /*
        * @brief 一维数组
        * @param typename T
        * @param size N
        */
        template<typename T, int N>
        struct Array1 final
        {
            static_assert(N > 0, "Array1 size must be positive.");

        public:
            /*
            * @brief 构造函数
            */
            Array1()
            {
                for (int i = 0; i < N; ++i)
                    m_Values[i] = T();
            }

            /*
            * @brief 构造函数
            * @param [in] vec_
            */
            Array1(IN const std::vector<T>& vec_)
            {
                int count = (int)vec_.size();
                for (int i = 0; i < N; ++i)
                {
                    if (count == 0)
                        m_Values[i] = T();
                    else if (i < count)
                        m_Values[i] = vec_[i];
                    else
                        m_Values[i] = vec_[count - 1]; // 不够用最后一个值填充
                }
            }

            /*
            * @brief 可变参数构造
            * @param [in] args
            */
            template<typename... Args>
            Array1(IN Args... args)
            {
                static_assert(sizeof...(Args) > 0, "At least one argument required");
                T tmp[] = { static_cast<T>(args)... };
                int count = sizeof...(Args);

                for (int i = 0; i < N; ++i)
                {
                    if (i < count)
                        m_Values[i] = tmp[i];
                    else
                        m_Values[i] = tmp[count - 1]; // 不够用最后一个值填充
                }
            }

        public:
            /*
            * @brief 索引器
            * @param [in] nIndex_
            * @return T&
            */
            T& operator[](IN const int nIndex_)
            {
                return m_Values[nIndex_];
            }

            /*
            * @brief 索引器
            * @param [in] nIndex_
            * @return const T&
            */
            const T& operator[](IN const int nIndex_) const
            {
                return m_Values[nIndex_];
            }

            /*
            * @brief 获取尺寸
            * return int
            */
            int size() const
            {
                return N;
            }

            /*
            * @brief 获取数据首地址
            * @return const T*
            */
            const T* GetData() const
            {
                return m_Values;
            }
        private:
            T m_Values[N];
        };

        // ---------------------- 算术运算 ----------------------
        //!< Array1 vs Array1
        template<typename T, int N, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array1<T, N> operator+(const Array1<T, N>& a, const Array1<T, N>& b) { Array1<T, N> r; for (int i = 0; i < N; ++i) r[i] = a[i] + b[i]; return r; }
        template<typename T, int N, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array1<T, N> operator-(const Array1<T, N>& a, const Array1<T, N>& b) { Array1<T, N> r; for (int i = 0; i < N; ++i) r[i] = a[i] - b[i]; return r; }
        template<typename T, int N, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array1<T, N> operator*(const Array1<T, N>& a, const Array1<T, N>& b) { Array1<T, N> r; for (int i = 0; i < N; ++i) r[i] = a[i] * b[i]; return r; }
        template<typename T, int N, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array1<T, N> operator/(const Array1<T, N>& a, const Array1<T, N>& b) { Array1<T, N> r; for (int i = 0; i < N; ++i) r[i] = a[i] / b[i]; return r; }
        template<typename T, int N, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array1<T, N> operator-(const Array1<T, N>& a) { Array1<T, N> r; for (int i = 0; i < N; ++i) r[i] = -a[i]; return r; }

        // Array1 vs 标量
        template<typename T, int N, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array1<T, N> operator+(const Array1<T, N>& a, T s) { Array1<T, N> r; for (int i = 0; i < N; ++i) r[i] = a[i] + s; return r; }
        template<typename T, int N, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array1<T, N> operator-(const Array1<T, N>& a, T s) { Array1<T, N> r; for (int i = 0; i < N; ++i) r[i] = a[i] - s; return r; }
        template<typename T, int N, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array1<T, N> operator*(const Array1<T, N>& a, T s) { Array1<T, N> r; for (int i = 0; i < N; ++i) r[i] = a[i] * s; return r; }
        template<typename T, int N, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array1<T, N> operator/(const Array1<T, N>& a, T s) { Array1<T, N> r; for (int i = 0; i < N; ++i) r[i] = a[i] / s; return r; }
        // 标量 vs Array1
        template<typename T, int N, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array1<T, N> operator+(T s, const Array1<T, N>& a) { Array1<T, N> r; for (int i = 0; i < N; ++i) r[i] = s + a[i]; return r; }
        template<typename T, int N, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array1<T, N> operator-(T s, const Array1<T, N>& a) { Array1<T, N> r; for (int i = 0; i < N; ++i) r[i] = s - a[i]; return r; }
        template<typename T, int N, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array1<T, N> operator*(T s, const Array1<T, N>& a) { Array1<T, N> r; for (int i = 0; i < N; ++i) r[i] = s * a[i]; return r; }
        template<typename T, int N, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array1<T, N> operator/(T s, const Array1<T, N>& a) { Array1<T, N> r; for (int i = 0; i < N; ++i) r[i] = s / a[i]; return r; }
        // 复合赋值 Array1 vs Array1
        template<typename T, int N, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array1<T, N>& operator+=(Array1<T, N>& a, const Array1<T, N>& b) { for (int i = 0; i < N; ++i) a[i] += b[i]; return a; }
        template<typename T, int N, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array1<T, N>& operator-=(Array1<T, N>& a, const Array1<T, N>& b) { for (int i = 0; i < N; ++i) a[i] -= b[i]; return a; }
        template<typename T, int N, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array1<T, N>& operator*=(Array1<T, N>& a, const Array1<T, N>& b) { for (int i = 0; i < N; ++i) a[i] *= b[i]; return a; }
        template<typename T, int N, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array1<T, N>& operator/=(Array1<T, N>& a, const Array1<T, N>& b) { for (int i = 0; i < N; ++i) a[i] /= b[i]; return a; }
        //!< 复合赋值 Array1 vs 标量
        template<typename T, int N, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array1<T, N>& operator+=(Array1<T, N>& a, T s) { for (int i = 0; i < N; ++i) a[i] += s; return a; }
        template<typename T, int N, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array1<T, N>& operator-=(Array1<T, N>& a, T s) { for (int i = 0; i < N; ++i) a[i] -= s; return a; }
        template<typename T, int N, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array1<T, N>& operator*=(Array1<T, N>& a, T s) { for (int i = 0; i < N; ++i) a[i] *= s; return a; }
        template<typename T, int N, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array1<T, N>& operator/=(Array1<T, N>& a, T s) { for (int i = 0; i < N; ++i) a[i] /= s; return a; }
        // ---------------------- 布尔运算 ----------------------
        //!< Array1 vs Array1
        template<int N> Array1<bool, N> operator&&(const Array1<bool, N>& a, const Array1<bool, N>& b) { Array1<bool, N> r; for (int i = 0; i < N; ++i) r[i] = a[i] && b[i]; return r; }
        template<int N> Array1<bool, N> operator||(const Array1<bool, N>& a, const Array1<bool, N>& b) { Array1<bool, N> r; for (int i = 0; i < N; ++i) r[i] = a[i] || b[i]; return r; }
        template<int N> Array1<bool, N> operator^(const Array1<bool, N>& a, const Array1<bool, N>& b) { Array1<bool, N> r; for (int i = 0; i < N; ++i) r[i] = a[i] ^ b[i]; return r; }
        template<int N> Array1<bool, N> operator!(const Array1<bool, N>& a) { Array1<bool, N> r; for (int i = 0; i < N; ++i) r[i] = !a[i]; return r; }
        // Array1 vs 标量
        template<int N> Array1<bool, N>& operator&=(Array1<bool, N>& a, const Array1<bool, N>& b) { for (int i = 0; i < N; ++i)a[i] &= b[i]; return a; }
        template<int N> Array1<bool, N>& operator|=(Array1<bool, N>& a, const Array1<bool, N>& b) { for (int i = 0; i < N; ++i)a[i] |= b[i]; return a; }
        template<int N> Array1<bool, N>& operator^=(Array1<bool, N>& a, const Array1<bool, N>& b) { for (int i = 0; i < N; ++i)a[i] ^= b[i]; return a; }
        // 标量 vs Array1
        template<int N> Array1<bool, N>& operator&=(Array1<bool, N>& a, bool s) { for (int i = 0; i < N; ++i)a[i] &= s; return a; }
        template<int N> Array1<bool, N>& operator|=(Array1<bool, N>& a, bool s) { for (int i = 0; i < N; ++i)a[i] |= s; return a; }
        template<int N> Array1<bool, N>& operator^=(Array1<bool, N>& a, bool s) { for (int i = 0; i < N; ++i)a[i] ^= s; return a; }
        // 复合赋值 Array1 vs Array1
        template<int N> Array1<bool, N> operator&&(const Array1<bool, N>& a, bool s) { Array1<bool, N> r; for (int i = 0; i < N; ++i) r[i] = a[i] && s; return r; }
        template<int N> Array1<bool, N> operator||(const Array1<bool, N>& a, bool s) { Array1<bool, N> r; for (int i = 0; i < N; ++i) r[i] = a[i] || s; return r; }
        template<int N> Array1<bool, N> operator^(const Array1<bool, N>& a, bool s) { Array1<bool, N> r; for (int i = 0; i < N; ++i) r[i] = a[i] ^ s; return r; }
        //!< 复合赋值 Array1 vs 标量
        template<int N> Array1<bool, N> operator&&(bool s, const Array1<bool, N>& a) { Array1<bool, N> r; for (int i = 0; i < N; ++i) r[i] = s && a[i]; return r; }
        template<int N> Array1<bool, N> operator||(bool s, const Array1<bool, N>& a) { Array1<bool, N> r; for (int i = 0; i < N; ++i) r[i] = s || a[i]; return r; }
        template<int N> Array1<bool, N> operator^(bool s, const Array1<bool, N>& a) { Array1<bool, N> r; for (int i = 0; i < N; ++i) r[i] = s ^ a[i]; return r; }
        // ---------------------- 比较运算 ----------------------
        template<typename T, int N> bool operator==(const Array1<T, N>& a, const Array1<T, N>& b) { for (int i = 0; i < N; ++i) if (!(a[i] == b[i])) { return false; } return true; }
        template<typename T, int N> bool operator!=(const Array1<T, N>& a, const Array1<T, N>& b) { return !(a == b); }
        template<typename T, int N> bool operator<(const Array1<T, N>& a, const Array1<T, N>& b) { for (int i = 0; i < N; ++i) { if (a[i] < b[i]) { return true; } if (b[i] < a[i]) { return false; } } return false; }
        template<typename T, int N> bool operator<=(const Array1<T, N>& a, const Array1<T, N>& b) { return !(b < a); }
        template<typename T, int N> bool operator>(const Array1<T, N>& a, const Array1<T, N>& b) { return b < a; }
        template<typename T, int N> bool operator>=(const Array1<T, N>& a, const Array1<T, N>& b) { return !(a < b); }
        // ---------------------- ostream 支持 ----------------------
        template<typename T, int N> std::ostream& operator<<(std::ostream& os, const Array1<T, N>& arr) { os << "["; for (int i = 0; i < N; ++i) { os << arr[i];     if (i != N - 1) os << ", "; }os << "]"; return os; }

        typedef Array1<bool, 1> Bool1;
        typedef Array1<bool, 2> Bool2;
        typedef Array1<bool, 3> Bool3;
        typedef Array1<bool, 4> Bool4;
        typedef Array1<bool, 5> Bool5;
        typedef Array1<bool, 6> Bool6;

        typedef Array1<unsigned char, 1> Byte1;
        typedef Array1<unsigned char, 2> Byte2;
        typedef Array1<unsigned char, 3> Byte3;
        typedef Array1<unsigned char, 4> Byte4;
        typedef Array1<unsigned char, 5> Byte5;
        typedef Array1<unsigned char, 6> Byte6;

        typedef Array1<short, 1> Short1;
        typedef Array1<short, 2> Short2;
        typedef Array1<short, 3> Short3;
        typedef Array1<short, 4> Short4;
        typedef Array1<short, 5> Short5;
        typedef Array1<short, 6> Short6;

        typedef Array1<int, 1> Int1;
        typedef Array1<int, 2> Int2;
        typedef Array1<int, 3> Int3;
        typedef Array1<int, 4> Int4;
        typedef Array1<int, 5> Int5;
        typedef Array1<int, 6> Int6;

        typedef Array1<long long, 1> Long1;
        typedef Array1<long long, 2> Long2;
        typedef Array1<long long, 3> Long3;
        typedef Array1<long long, 4> Long4;
        typedef Array1<long long, 5> Long5;
        typedef Array1<long long, 6> Long6;

        typedef Array1<float, 1> Real1;
        typedef Array1<float, 2> Real2;
        typedef Array1<float, 3> Real3;
        typedef Array1<float, 4> Real4;
        typedef Array1<float, 5> Real5;
        typedef Array1<float, 6> Real6;

        typedef Array1<double, 1> Double1;
        typedef Array1<double, 2> Double2;
        typedef Array1<double, 3> Double3;
        typedef Array1<double, 4> Double4;
        typedef Array1<double, 5> Double5;
        typedef Array1<double, 6> Double6;
    }
}

namespace std
{
    template<typename T, int N>
    struct hash<iCAX::Data::Array1<T, N>>
    {
        std::size_t operator()(const iCAX::Data::Array1<T, N>& arr) const
        {
            std::size_t seed = 0;
            for (int i = 0; i < N; ++i)
                seed ^= std::hash<T>{}(arr[i]) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
            return seed;
        }
    };
}
