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
        * @brief 二维数组
        * @param typename T
        * @param size R 行
        * @param size C 列
        */
        template<typename T, int R, int C>
        struct Array2 final
        {
            static_assert(R > 0 && C > 0, "Array2 row and column counts must be positive.");

        public:
            /*
            * @brief 构造函数
            */
            Array2()
            {
                for (int i = 0; i < R; ++i)
                {
                    for (int j = 0; j < C; ++j)
                    {
                        m_Values[i][j] = T();
                    }
                }
            }

            /*
            * @brief 构造函数
            * @param [in] vec_
            */
            Array2(IN const std::vector<T>& vec_)
            {
                int count = (int)vec_.size();

                int idx = 0;
                for (int i = 0; i < R; ++i)
                {
                    for (int j = 0; j < C; ++j)
                    {
                        if (count == 0)
                            m_Values[i][j] = T();
                        else if (idx < count)
                            m_Values[i][j] = vec_[idx++];
                        else
                            m_Values[i][j] = vec_[count - 1]; // 不够用就用最后一个填充
                    }
                }
            }


            /*
            * @brief 可变参数构造
            * @param [in] args
            */
            template<typename... Args>
            Array2(IN Args... args)
            {
                static_assert(sizeof...(Args) > 0, "At least one argument required");
                T tmp[] = { static_cast<T>(args)... };
                int count = sizeof...(Args);

                int idx = 0;
                for (int i = 0; i < R; ++i)
                {
                    for (int j = 0; j < C; ++j)
                    {
                        if (idx < count)
                            m_Values[i][j] = tmp[idx++];
                        else
                            m_Values[i][j] = tmp[count - 1]; // 不够用就用最后一个填充
                    }
                }
            }

            /*
            * @brief 索引器
            * @param [in] nRow_ 行索引
            * @param [in] nCol_ 列索引
            */
            T& operator()(IN const int nRow_, IN const int nCol_)
            {
                return m_Values[nRow_][nCol_];
            }

            /*
            * @brief 索引器
            * @param [in] nRow_ 行索引
            * @param [in] nCol_ 列索引
            */
            const T& operator()(IN const int nRow_, IN const int nCol_) const
            {
                return m_Values[nRow_][nCol_];
            }

            /*
            * @brief 获取行数
            * @return int
            */
            int Rows() const
            {
                return R;
            }

            /*
            * @brief 获取列数
            * @return int
            */
            int Cols() const
            {
                return C;
            }

            /*
            * @brief 获取数据首地址
            * @return const T*
            */
            const T* GetData() const
            {
                return &m_Values[0][0];
            }

        private:
            T m_Values[R][C];
        };

        // ---------------------- 算术运算 ----------------------
        //!< Array2<T,R,C> vs Array2<T,R,C>
        template<typename T, int R, int C, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array2<T, R, C> operator+(const Array2<T, R, C>& a, const Array2<T, R, C>& b) { Array2<T, R, C> r; for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) r(i, j) = a(i, j) + b(i, j); return r; }
        template<typename T, int R, int C, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array2<T, R, C> operator-(const Array2<T, R, C>& a, const Array2<T, R, C>& b) { Array2<T, R, C> r; for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) r(i, j) = a(i, j) - b(i, j); return r; }
        template<typename T, int R, int C, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array2<T, R, C> operator*(const Array2<T, R, C>& a, const Array2<T, R, C>& b) { Array2<T, R, C> r; for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) r(i, j) = a(i, j) * b(i, j); return r; }
        template<typename T, int R, int C, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array2<T, R, C> operator/(const Array2<T, R, C>& a, const Array2<T, R, C>& b) { Array2<T, R, C> r; for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) r(i, j) = a(i, j) / b(i, j); return r; }
        //!< Array2<T,R,C> vs T
        template<typename T, int R, int C, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array2<T, R, C> operator+(const Array2<T, R, C>& a, T s) { Array2<T, R, C> r; for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) r(i, j) = a(i, j) + s; return r; }
        template<typename T, int R, int C, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array2<T, R, C> operator-(const Array2<T, R, C>& a, T s) { Array2<T, R, C> r; for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) r(i, j) = a(i, j) - s; return r; }
        template<typename T, int R, int C, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array2<T, R, C> operator*(const Array2<T, R, C>& a, T s) { Array2<T, R, C> r; for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) r(i, j) = a(i, j) * s; return r; }
        template<typename T, int R, int C, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array2<T, R, C> operator/(const Array2<T, R, C>& a, T s) { Array2<T, R, C> r; for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) r(i, j) = a(i, j) / s; return r; }
        //!< T vs Array2<T,R,C>
        template<typename T, int R, int C, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array2<T, R, C> operator+(T s, const Array2<T, R, C>& a) { return a + s; }
        template<typename T, int R, int C, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array2<T, R, C> operator-(T s, const Array2<T, R, C>& a) { Array2<T, R, C> r; for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) r(i, j) = s - a(i, j); return r; }
        template<typename T, int R, int C, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array2<T, R, C> operator*(T s, const Array2<T, R, C>& a) { return a * s; }
        template<typename T, int R, int C, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array2<T, R, C> operator/(T s, const Array2<T, R, C>& a) { Array2<T, R, C> r; for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) r(i, j) = s / a(i, j); return r; }
        //!< 复合 Array2<T,R,C> vs Array2<T,R,C>
        template<typename T, int R, int C, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array2<T, R, C>& operator+=(Array2<T, R, C>& a, const Array2<T, R, C>& b) { for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) a(i, j) += b(i, j); return a; }
        template<typename T, int R, int C, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array2<T, R, C>& operator-=(Array2<T, R, C>& a, const Array2<T, R, C>& b) { for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) a(i, j) -= b(i, j); return a; }
        template<typename T, int R, int C, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array2<T, R, C>& operator*=(Array2<T, R, C>& a, const Array2<T, R, C>& b) { for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) a(i, j) *= b(i, j); return a; }
        template<typename T, int R, int C, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array2<T, R, C>& operator/=(Array2<T, R, C>& a, const Array2<T, R, C>& b) { for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) a(i, j) /= b(i, j); return a; }
        //!< 复合 Array2<T,R,C> vs T
        template<typename T, int R, int C, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array2<T, R, C>& operator+=(Array2<T, R, C>& a, T s) { for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) a(i, j) += s; return a; }
        template<typename T, int R, int C, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array2<T, R, C>& operator-=(Array2<T, R, C>& a, T s) { for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) a(i, j) -= s; return a; }
        template<typename T, int R, int C, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array2<T, R, C>& operator*=(Array2<T, R, C>& a, T s) { for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) a(i, j) *= s; return a; }
        template<typename T, int R, int C, typename = std::enable_if_t<std::is_arithmetic_v<T>>> Array2<T, R, C>& operator/=(Array2<T, R, C>& a, T s) { for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) a(i, j) /= s; return a; }
        // ---------------------- bool型运算 ----------------------
        //!< Array2<bool,R,C> vs Array2<bool,R,C>
        template<int R, int C> iCAX::Data::Array2<bool, R, C> operator&&(const iCAX::Data::Array2<bool, R, C>& a, const iCAX::Data::Array2<bool, R, C>& b) { iCAX::Data::Array2<bool, R, C> r; for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) r(i, j) = a(i, j) && b(i, j); return r; }
        template<int R, int C> iCAX::Data::Array2<bool, R, C> operator||(const iCAX::Data::Array2<bool, R, C>& a, const iCAX::Data::Array2<bool, R, C>& b) { iCAX::Data::Array2<bool, R, C> r; for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) r(i, j) = a(i, j) || b(i, j); return r; }
        template<int R, int C> iCAX::Data::Array2<bool, R, C> operator^(const iCAX::Data::Array2<bool, R, C>& a, const iCAX::Data::Array2<bool, R, C>& b) { iCAX::Data::Array2<bool, R, C> r; for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) r(i, j) = a(i, j) ^ b(i, j); return r; }
        template<int R, int C> iCAX::Data::Array2<bool, R, C> operator!(const iCAX::Data::Array2<bool, R, C>& a) { iCAX::Data::Array2<bool, R, C> r; for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) r(i, j) = !a(i, j); return r; }
        //!< Array2<bool,R,C> vs bool
        template<int R, int C> iCAX::Data::Array2<bool, R, C> operator&&(const iCAX::Data::Array2<bool, R, C>& a, bool s) { iCAX::Data::Array2<bool, R, C> r; for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) r(i, j) = a(i, j) && s; return r; }
        template<int R, int C> iCAX::Data::Array2<bool, R, C> operator||(const iCAX::Data::Array2<bool, R, C>& a, bool s) { iCAX::Data::Array2<bool, R, C> r; for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) r(i, j) = a(i, j) || s; return r; }
        template<int R, int C> iCAX::Data::Array2<bool, R, C> operator^(const iCAX::Data::Array2<bool, R, C>& a, bool s) { iCAX::Data::Array2<bool, R, C> r; for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) r(i, j) = a(i, j) ^ s; return r; }
        //!< bool vs Array2<bool,R,C>
        template<int R, int C> iCAX::Data::Array2<bool, R, C> operator&&(bool s, const iCAX::Data::Array2<bool, R, C>& a) { iCAX::Data::Array2<bool, R, C> r; for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) r(i, j) = s && a(i, j); return r; }
        template<int R, int C> iCAX::Data::Array2<bool, R, C> operator||(bool s, const iCAX::Data::Array2<bool, R, C>& a) { iCAX::Data::Array2<bool, R, C> r; for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) r(i, j) = s || a(i, j); return r; }
        template<int R, int C> iCAX::Data::Array2<bool, R, C> operator^(bool s, const iCAX::Data::Array2<bool, R, C>& a) { iCAX::Data::Array2<bool, R, C> r; for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) r(i, j) = s ^ a(i, j); return r; }
        // 复合赋值 Array2<bool,R,C> op= Array2<bool,R,C>
        template<int R, int C> iCAX::Data::Array2<bool, R, C>& operator&=(iCAX::Data::Array2<bool, R, C>& a, const iCAX::Data::Array2<bool, R, C>& b) { for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) a(i, j) &= b(i, j); return a; }
        template<int R, int C> iCAX::Data::Array2<bool, R, C>& operator|=(iCAX::Data::Array2<bool, R, C>& a, const iCAX::Data::Array2<bool, R, C>& b) { for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) a(i, j) |= b(i, j); return a; }
        template<int R, int C> iCAX::Data::Array2<bool, R, C>& operator^=(iCAX::Data::Array2<bool, R, C>& a, const iCAX::Data::Array2<bool, R, C>& b) { for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) a(i, j) ^= b(i, j); return a; }
        // 复合赋值 Array2<bool,R,C> op= 标量
        template<int R, int C> iCAX::Data::Array2<bool, R, C>& operator&=(iCAX::Data::Array2<bool, R, C>& a, bool s) { for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) a(i, j) &= s; return a; }
        template<int R, int C> iCAX::Data::Array2<bool, R, C>& operator|=(iCAX::Data::Array2<bool, R, C>& a, bool s) { for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) a(i, j) |= s; return a; }
        template<int R, int C> iCAX::Data::Array2<bool, R, C>& operator^=(iCAX::Data::Array2<bool, R, C>& a, bool s) { for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) a(i, j) ^= s; return a; }
        // ---------------------- 比较运算 ----------------------
        template<typename T, int R, int C> bool operator==(const Array2<T, R, C>& a, const Array2<T, R, C>& b) { for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) if (!(a(i, j) == b(i, j))) return false; return true; }
        template<typename T, int R, int C> bool operator!=(const Array2<T, R, C>& a, const Array2<T, R, C>& b) { return !(a == b); }
        template<typename T, int R, int C> bool operator<(const Array2<T, R, C>& a, const Array2<T, R, C>& b) { for (int i = 0; i < R; ++i) for (int j = 0; j < C; ++j) { if (a(i, j) < b(i, j)) return true; if (b(i, j) < a(i, j)) return false; } return false; }
        // ---------------------- 输出流 ----------------------
        template<typename T, int R, int C> std::ostream& operator<<(std::ostream& os, const Array2<T, R, C>& a) { os << "["; for (int i = 0; i < R; ++i) { os << "["; for (int j = 0; j < C; ++j) { os << a(i, j); if (j != C - 1) os << ", "; } os << "]"; if (i != R - 1) os << ", "; } os << "]"; return os; }


        //!< 常用 typedef
        typedef Array2<bool, 2, 2> Bool2x2;
        typedef Array2<bool, 3, 3> Bool3x3;
        typedef Array2<bool, 4, 4> Bool4x4;

        typedef Array2<unsigned char, 2, 2> Byte2x2;
        typedef Array2<unsigned char, 3, 3> Byte3x3;
        typedef Array2<unsigned char, 4, 4> Byte4x4;

        typedef Array2<short, 2, 2> Short2x2;
        typedef Array2<short, 3, 3> Short3x3;
        typedef Array2<short, 4, 4> Short4x4;

        typedef Array2<int, 2, 2> Int2x2;
        typedef Array2<int, 3, 3> Int3x3;
        typedef Array2<int, 4, 4> Int4x4;

        typedef Array2<long long, 2, 2> Long2x2;
        typedef Array2<long long, 3, 3> Long3x3;
        typedef Array2<long long, 4, 4> Long4x4;

        typedef Array2<float, 2, 2> Real2x2;
        typedef Array2<float, 3, 3> Real3x3;
        typedef Array2<float, 4, 4> Real4x4;

        typedef Array2<double, 2, 2> Double2x2;
        typedef Array2<double, 3, 3> Double3x3;
        typedef Array2<double, 4, 4> Double4x4;
    }
}

// -------------------- Hash 支持 --------------------
namespace std
{
    template<typename T, int R, int C>
    struct hash<iCAX::Data::Array2<T, R, C>>
    {
        std::size_t operator()(const iCAX::Data::Array2<T, R, C>& a) const noexcept
        {
            std::size_t seed = 0;
            std::hash<T> h;
            for (int i = 0; i < R; i++)
                for (int j = 0; j < C; j++)
                    seed ^= h(a(i, j)) + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
            return seed;
        }
    };
}
