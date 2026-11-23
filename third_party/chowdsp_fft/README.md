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
partitioning and reset checks. The actual VST3 comparison also passes the
Intruders distortion fixture and preserves the SikBit full-effects comparisons
at 44.1 and 48 kHz. These are bounded fixture results, not full-product parity.

The e-table setup uses the bounded-angle sinf/cosf polynomial from public
AMD win-libm (https://github.com/amd/win-libm), with its MIT notice retained.
Eight inverse-real endpoint operations explicitly fuse their multiply/add;
other arithmetic retains contraction disabled. Independent original convolution
replay matches 402432 channel samples exactly at 512-frame host boundaries.
Product event-boundary integration remains pending; this is component evidence.
