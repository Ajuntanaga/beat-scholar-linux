# chowdsp_fft

Source: https://github.com/Chowdhury-DSP/chowdsp_fft
Revision: d951429994ef1095fc2bfe9d2573ed6dcfcff44d

This subset supports the Linux x86 build. Copyright and FFTPACK license notices
are retained in each source file and LICENSE.txt. Include LICENSE.txt with binary distributions.

Local changes: evaluate setup sine/cosine in double precision after rounding
angles to float; disable implicit floating-point contraction (explicit FMA
intrinsics remain); require OSXSAVE state before selecting AVX; handle failed setup allocation without dereferencing null; use a small
position-independent CMake target with separately compiled AVX2/FMA objects.

The reference spectrum and critical wet sample match in the independent replay.
Streaming integration passes the shared-core audio tests, including callback
partitioning and reset checks. Full plugin verification remains pending.
