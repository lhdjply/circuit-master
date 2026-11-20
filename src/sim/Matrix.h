#pragma once

#include <QString>
#include "core/Language.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace csm {

/// 稠密矩阵 + 部分主元高斯消元。规模在数百阶以内时性能足够，
/// 且同时支持实数与复数（交流分析使用）。
template <typename T>
class Matrix {
public:
    Matrix() = default;
    explicit Matrix(int n) { resize(n); }

    void resize(int n)
    {
        m_n = n;
        m_a.assign(size_t(n) * size_t(n), T(0));
    }

    void zero() { std::fill(m_a.begin(), m_a.end(), T(0)); }

    int size() const { return m_n; }

    T& operator()(int r, int c) { return m_a[size_t(r) * size_t(m_n) + size_t(c)]; }
    const T& operator()(int r, int c) const { return m_a[size_t(r) * size_t(m_n) + size_t(c)]; }

    /// 解 A x = b。b 会被保留，结果写入 x。
    bool solve(const std::vector<T>& b, std::vector<T>& x, QString* error = nullptr) const
    {
        const int n = m_n;
        if (n == 0) {
            x.clear();
            return true;
        }

        std::vector<T> a = m_a;
        std::vector<T> rhs = b;

        for (int k = 0; k < n; ++k) {
            int pivot = k;
            double best = std::abs(a[size_t(k) * size_t(n) + size_t(k)]);
            for (int i = k + 1; i < n; ++i) {
                const double v = std::abs(a[size_t(i) * size_t(n) + size_t(k)]);
                if (v > best) {
                    best = v;
                    pivot = i;
                }
            }
            if (!(best > 1e-13)) {
                if (error)
                    *error = CSM_TR("Singular linear system (tiny pivot at row %1)").arg(k + 1);
                return false;
            }
            if (pivot != k) {
                for (int j = k; j < n; ++j)
                    std::swap(a[size_t(k) * size_t(n) + size_t(j)],
                              a[size_t(pivot) * size_t(n) + size_t(j)]);
                std::swap(rhs[size_t(k)], rhs[size_t(pivot)]);
            }

            const T diag = a[size_t(k) * size_t(n) + size_t(k)];
            for (int i = k + 1; i < n; ++i) {
                const T factor = a[size_t(i) * size_t(n) + size_t(k)] / diag;
                if (factor == T(0))
                    continue;
                a[size_t(i) * size_t(n) + size_t(k)] = T(0);
                for (int j = k + 1; j < n; ++j)
                    a[size_t(i) * size_t(n) + size_t(j)] -=
                        factor * a[size_t(k) * size_t(n) + size_t(j)];
                rhs[size_t(i)] -= factor * rhs[size_t(k)];
            }
        }

        x.assign(size_t(n), T(0));
        for (int i = n - 1; i >= 0; --i) {
            T sum = rhs[size_t(i)];
            for (int j = i + 1; j < n; ++j)
                sum -= a[size_t(i) * size_t(n) + size_t(j)] * x[size_t(j)];
            x[size_t(i)] = sum / a[size_t(i) * size_t(n) + size_t(i)];
        }
        return true;
    }

private:
    int m_n = 0;
    std::vector<T> m_a;
};

} // namespace csm
