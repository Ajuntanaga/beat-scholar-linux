#pragma once

#include <cmath>

namespace bsl::detail {
// Preserve the reference's float rounding before the high-gain DC/clip stages.
// Explicit fused operations make the rational approximation independent of libm.
inline float distortionAtan(float input) noexcept
{
    if (input == 0.0f || std::isnan(input)) return input;
    double x = std::abs(static_cast<double>(input));
    double offset = 0.0;
    if (x >= 0.4375)
    {
        if (x < 0.6875)
        {
            offset = 0x1.dac670561bb4fp-2;
            x = (2.0 * x - 1.0) / (x + 2.0);
        }
        else if (x < 1.1875)
        {
            offset = 0x1.921fb54442d18p-1;
            x = (x - 1.0) / (x + 1.0);
        }
        else if (x < 2.4375)
        {
            offset = 0x1.f730bd281f69bp-1;
            x = (x - 1.5) / std::fma(1.5, x, 1.0);
        }
        else
        {
            offset = 0x1.921fb54442d18p+0;
            x = -1.0 / x;
        }
    }
    const double square = x * x;
    const double numerator = std::fma(std::fma(0x1.3476a758da22ap-8, square,
        0x1.89e173a81ee7fp-3), square, 0x1.2fa5316907834p-2) * (x * square);
    const double denominator = std::fma(std::fma(0x1.327e3df2cf2aap-2, square,
        0x1.1c58793da6ea4p+0), square, 0x1.c777ca21053f0p-1);
    const double result = offset - (numerator / denominator - x);
    return static_cast<float>(std::signbit(input) ? -result : result);
}
} // namespace bsl::detail
