#pragma once
#include <algorithm>
#include <cmath>

namespace pcv::planefitting::detail {
inline void symmetricEigen3(const double input[3][3], double values[3], double vectors[3][3])
{
    double a[3][3];
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) {
            a[r][c] = input[r][c];
            vectors[r][c] = r == c ? 1.0 : 0.0;
        }
    for (int iteration = 0; iteration < 24; ++iteration) {
        int p = 0;
        int q = 1;
        double largest = std::abs(a[0][1]);
        if (std::abs(a[0][2]) > largest) { p = 0; q = 2; largest = std::abs(a[0][2]); }
        if (std::abs(a[1][2]) > largest) { p = 1; q = 2; largest = std::abs(a[1][2]); }
        if (largest < 1.0e-12) break;
        const double angle = 0.5 * std::atan2(2.0 * a[p][q], a[q][q] - a[p][p]);
        const double cs = std::cos(angle);
        const double sn = std::sin(angle);
        for (int k = 0; k < 3; ++k) {
            const double apk = a[p][k], aqk = a[q][k];
            a[p][k] = cs * apk - sn * aqk;
            a[q][k] = sn * apk + cs * aqk;
        }
        for (int k = 0; k < 3; ++k) {
            const double akp = a[k][p], akq = a[k][q];
            a[k][p] = cs * akp - sn * akq;
            a[k][q] = sn * akp + cs * akq;
            const double vkp = vectors[k][p], vkq = vectors[k][q];
            vectors[k][p] = cs * vkp - sn * vkq;
            vectors[k][q] = sn * vkp + cs * vkq;
        }
    }
    values[0] = a[0][0]; values[1] = a[1][1]; values[2] = a[2][2];
    for (int i = 0; i < 3; ++i) {
        int best = i;
        for (int j = i + 1; j < 3; ++j)
            if (values[j] > values[best]) best = j;
        if (best == i) continue;
        std::swap(values[i], values[best]);
        for (int r = 0; r < 3; ++r) std::swap(vectors[r][i], vectors[r][best]);
    }
}


} // namespace pcv::planefitting::detail
