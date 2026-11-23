/**
Copyright (c) 2024 Jatin Chowdhury ( jatin@chowdsp.com )
Copyright (c) 2013  Julien Pommier ( pommier@modartt.com )

Based on original fortran 77 code from FFTPACKv4 from NETLIB,
authored by Dr Paul Swarztrauber of NCAR, in 1985.

As confirmed by the NCAR fftpack software curators, the following
FFTPACKv5 license applies to FFTPACKv4 sources. My changes are
released under the same terms.

FFTPACK license:
http://www.cisl.ucar.edu/css/software/fftpack5/ftpk.html

Copyright (c) 2004 the University Corporation for Atmospheric
Research ("UCAR"). All rights reserved. Developed by NCAR's
Computational and Information Systems Laboratory, UCAR,
www.cisl.ucar.edu.

Redistribution and use of the Software in source and binary forms,
with or without modification, is permitted provided that the
following conditions are met:

- Neither the names of NCAR's Computational and Information Systems
  Laboratory, the University Corporation for Atmospheric Research,
  nor the names of its sponsors or contributors may be used to
  endorse or promote products derived from this Software without
  specific prior written permission.

- Redistributions of source code must retain the above copyright
  notices, this list of conditions, and the disclaimer below.

- Redistributions in binary form must reproduce the above copyright
  notice, this list of conditions, and the disclaimer below in the
  documentation and/or other materials provided with the
  distribution.

THIS SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE CONTRIBUTORS OR COPYRIGHT
HOLDERS BE LIABLE FOR ANY CLAIM, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES OR OTHER LIABILITY, WHETHER IN AN
ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS WITH THE
SOFTWARE.
*/

/*
MIT License
-----------

Copyright (c) 2002-2019 Advanced Micro Devices, Inc.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this Software and associated documentaon files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/
#include <cstdint>
#include <cstring>
namespace chowdsp::fft::common
{
// Adapted from AMD win-libm sinf/cosf (MIT notice above). FFT setup
// angles are bounded by 2*pi; general-purpose large-angle reduction is unnecessary.
static inline double coefficient (std::uint64_t bits)
{
    double value;
    std::memcpy (&value, &bits, sizeof (value));
    return value;
}

static inline double reducedSine (double x)
{
    const double z = x * x;
    double p = std::fma (z, coefficient (0x3ec71de3a556c734), coefficient (0xbf2a01a01a01a01a));
    p = std::fma (p, z, coefficient (0x3f81111111111111));
    p = std::fma (p, z, coefficient (0xbfc5555555555555));
    return std::fma (p, x * z, x);
}

static inline double reducedCosine (double x)
{
    const double z = x * x;
    double p = std::fma (z, coefficient (0xbe927e4fb7789f5c), coefficient (0x3efa01a01a01a019));
    p = std::fma (p, z, coefficient (0xbf56c16c16c16c16));
    p = std::fma (p, z, coefficient (0x3fa5555555555555));
    return std::fma (p, z * z, std::fma (-0.5, z, 1.0));
}

static inline float setupTrig (float angle, bool cosine)
{
    const double x = std::abs (static_cast<double> (angle));
    const int quadrant = static_cast<int> (std::fma (x, coefficient (0x3fe45f306dc9c883), 0.5));
    const double reduced = std::fma (-static_cast<double> (quadrant), coefficient (0x3ff921fb54400000), x)
        - static_cast<double> (quadrant) * coefficient (0x3dd0b4611a626331);
    double value;
    if (cosine)
    {
        value = quadrant % 2 ? reducedSine (reduced) : reducedCosine (reduced);
        if ((quadrant + 1) & 2) value = -value;
    }
    else
    {
        value = quadrant % 2 ? reducedCosine (reduced) : reducedSine (reduced);
        if (quadrant & 2) value = -value;
        if (std::signbit (angle)) value = -value;
    }
    return static_cast<float> (value);
}

static inline int decompose (int n, int* ifac, const int* ntryh)
{
    int nl = n, nf = 0, i, j;
    for (j = 0; ntryh[j]; ++j)
    {
        int ntry = ntryh[j];
        while (nl != 1)
        {
            int nq = nl / ntry;
            int nr = nl - ntry * nq;
            if (nr == 0)
            {
                ifac[2 + nf++] = ntry;
                nl = nq;
                if (ntry == 2 && nf != 1)
                {
                    for (i = 2; i <= nf; ++i)
                    {
                        int ib = nf - i + 2;
                        ifac[ib + 1] = ifac[ib];
                    }
                    ifac[2] = 2;
                }
            }
            else
                break;
        }
    }
    ifac[0] = n;
    ifac[1] = nf;
    return nf;
}

static inline void rffti1_ps (int n, float* wa, int* ifac)
{
    static constexpr int ntryh[] = { 4, 2, 3, 5, 0 };
    int k1, j, ii;

    int nf = decompose (n, ifac, ntryh);
    float argh = float (2 * M_PI) / (float) n;
    int is = 0;
    int nfm1 = nf - 1;
    int l1 = 1;
    for (k1 = 1; k1 <= nfm1; k1++)
    {
        int ip = ifac[k1 + 1];
        int ld = 0;
        int l2 = l1 * ip;
        int ido = n / l2;
        int ipm = ip - 1;
        for (j = 1; j <= ipm; ++j)
        {
            float argld;
            int i = is, fi = 0;
            ld += l1;
            argld = (float) ld * argh;
            for (ii = 3; ii <= ido; ii += 2)
            {
                i += 2;
                fi += 1;
                wa[i - 2] = std::cos ((double) ((float) fi * argld));
                wa[i - 1] = std::sin ((double) ((float) fi * argld));
            }
            is += ido;
        }
        l1 = l2;
    }
}

void inline cffti1_ps (int n, float* wa, int* ifac)
{
    static constexpr int ntryh[] = { 5, 3, 4, 2, 0 };
    int k1, j, ii;

    int nf = decompose (n, ifac, ntryh);
    float argh = float (2 * M_PI) / (float) n;
    int i = 1;
    int l1 = 1;
    for (k1 = 1; k1 <= nf; k1++)
    {
        int ip = ifac[k1 + 1];
        int ld = 0;
        int l2 = l1 * ip;
        int ido = n / l2;
        int idot = ido + ido + 2;
        int ipm = ip - 1;
        for (j = 1; j <= ipm; j++)
        {
            float argld;
            int i1 = i, fi = 0;
            wa[i - 1] = 1;
            wa[i] = 0;
            ld += l1;
            argld = (float) ld * argh;
            for (ii = 4; ii <= idot; ii += 2)
            {
                i += 2;
                fi += 1;
                wa[i - 1] = std::cos ((double) ((float) fi * argld));
                wa[i] = std::sin ((double) ((float) fi * argld));
            }
            if (ip > 5)
            {
                wa[i1 - 1] = wa[i - 1];
                wa[i1] = wa[i];
            }
        }
        l1 = l2;
    }
}

template <typename Setup_Type, typename SIMD_Type>
static inline Setup_Type* fft_new_setup (int N, fft_transform_t transform, int SIMD_SZ, void* data)
{
    /* unfortunately, the fft size must be a multiple of 16 for complex FFTs
       and 32 for real FFTs -- a lot of stuff would need to be rewritten to
       handle other cases (or maybe just switch to a scalar fft, I don't know..) */
    if (transform == FFT_REAL)
    {
        if (! ((N % (2 * SIMD_SZ * SIMD_SZ)) == 0 && N > 0))
            return nullptr;
    }
    if (transform == FFT_COMPLEX)
    {
        if (! ((N % (SIMD_SZ * SIMD_SZ)) == 0 && N > 0))
            return nullptr;
    }

    const auto Ncvec = (transform == FFT_REAL ? N / 2 : N) / SIMD_SZ;
    const auto data_bytes = 2 * Ncvec * sizeof (float) * SIMD_SZ;
    auto* s_data = (std::byte*) data;

    auto* s = (Setup_Type*) (s_data + data_bytes);
    s->N = N;
    s->transform = transform;
    /* nb of complex simd vectors */
    s->Ncvec = Ncvec;
    s->data = (SIMD_Type*) s_data;
    s->e = (float*) s->data;
    s->twiddle = (float*) (s->data + (2 * s->Ncvec * (SIMD_SZ - 1)) / SIMD_SZ);

    int k, m;
    const auto M = SIMD_SZ - 1;

    for (k = 0; k < s->Ncvec; ++k)
    {
        int i = k / (int) SIMD_SZ;
        int j = k % (int) SIMD_SZ;
        for (m = 0; m < M; ++m)
        {
            const auto A = static_cast<float> (-2 * M_PI * (m + 1) * k / N);
            s->e[(2 * (i * M + m) + 0) * SIMD_SZ + j] = setupTrig (A, true);
            s->e[(2 * (i * M + m) + 1) * SIMD_SZ + j] = setupTrig (A, false);
        }
    }

    if (transform == FFT_REAL)
    {
        rffti1_ps (N / (int) SIMD_SZ, s->twiddle, s->ifac);
    }
    else
    {
        cffti1_ps (N / (int) SIMD_SZ, s->twiddle, s->ifac);
    }

    /* check that N is decomposable with allowed prime factors */
    for (k = 0, m = 1; k < s->ifac[1]; ++k)
    {
        m *= s->ifac[2 + k];
    }
    if (m != N / SIMD_SZ)
    {
        fft_destroy_setup (s);
        s = nullptr;
    }

    return s;
}
}
