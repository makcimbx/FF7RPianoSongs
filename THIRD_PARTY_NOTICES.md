# Third-Party Notices

This file covers third-party material incorporated into the distributed
`FF7RPianoSongs.asi`. The project's first-party code is licensed separately
under the MIT License in [`LICENSE`](LICENSE).

The ASI statically links MinHook, midifile, muscriptor.cpp, and ggml, compiles
miniaudio's single-header implementation, and contains a C++ implementation
derived from portions of VGAudio. The package also ships the MuScriptor model
weights `muscriptor-small-f16.gguf`, which are licensed under CC BY-NC 4.0, not
MIT. The repository's Python, CMake, compiler, self-test executables, and
standalone helper tools are build or development tools and are not distributed
as part of the ASI package. In particular, the VGAudio .NET assembly used by the
repository's standalone `FF7RHcaTool` is not linked into or shipped with the ASI.

## MinHook

Use: statically linked x64 function-hooking library, including its bundled
Hacker Disassembler Engine 64 implementation.

Upstream: <https://github.com/TsudaKageyu/minhook>

Pinned source: commit `c3fcafdc10146beb5919319d0683e44e3c30d537`
(the upstream `v1.3.4` release commit).

License source: <https://github.com/TsudaKageyu/minhook/blob/c3fcafdc10146beb5919319d0683e44e3c30d537/LICENSE.txt>

The following is reproduced from the pinned source's `LICENSE.txt`:

```text
MinHook - The Minimalistic API Hooking Library for x64/x86
Copyright (C) 2009-2017 Tsuda Kageyu.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

================================================================================
Portions of this software are Copyright (c) 2008-2009, Vyacheslav Patkov.
================================================================================
Hacker Disassembler Engine 32 C
Copyright (c) 2008-2009, Vyacheslav Patkov.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

-------------------------------------------------------------------------------
Hacker Disassembler Engine 64 C
Copyright (c) 2008-2009, Vyacheslav Patkov.
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

 1. Redistributions of source code must retain the above copyright
    notice, this list of conditions and the following disclaimer.
 2. Redistributions in binary form must reproduce the above copyright
    notice, this list of conditions and the following disclaimer in the
    documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## miniaudio

Use: miniaudio's single-header implementation, including its integrated
`dr_wav`, `dr_flac`, and `dr_mp3` decoders and resampling code, is compiled into
the ASI for offline WAV, FLAC, and MP3 decoding and conversion to 48 kHz stereo.

Upstream: <https://github.com/mackron/miniaudio>

Pinned source: commit `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d`
(the upstream `0.11.25` release commit).

License sources:

- <https://github.com/mackron/miniaudio/blob/9634bedb5b5a2ca38c1ee7108a9358a4e233f14d/LICENSE>
- <https://github.com/mackron/miniaudio/blob/9634bedb5b5a2ca38c1ee7108a9358a4e233f14d/miniaudio.h>

miniaudio offers a choice between a public-domain dedication and the MIT No
Attribution license (MIT-0). This distribution selects MIT-0; it does not rely
on or claim the public-domain alternative.

At this pinned revision, the root `LICENSE` identifies `Copyright 2025 David
Reid`, while the license block embedded in the exact `miniaudio.h` compiled into
the ASI identifies `Copyright 2026 David Reid`. The following MIT-0 text is
reproduced from that incorporated header:

```text
Copyright 2026 David Reid

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## midifile

Use: statically linked library for parsing Standard MIDI Files and tempo maps.

Upstream: <https://github.com/craigsapp/midifile>

Pinned source: commit `98917df5b1bf0d6e8d4c0e5fff86d6b05343e793`.

License source: <https://github.com/craigsapp/midifile/blob/98917df5b1bf0d6e8d4c0e5fff86d6b05343e793/LICENSE.txt>

The following is reproduced from the pinned source's `LICENSE.txt`:

```text
Copyright (c) 1999-2018, Craig Stuart Sapp
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice,
   and the following disclaimer in the documentation and/or other materials
   provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

```

## VGAudio

Use: the ASI's native fixed-profile HCA encoder is a C++ port derived from the
CriHca encoder, packing, table, HCA writer, MDCT, bit-writer, and CRC16 portions
of VGAudio 2.2.1. VGAudio itself is not linked into or shipped with the ASI.

Upstream: <https://github.com/Thealexbarney/VGAudio>

Source revision: tag `v2.2.1`, commit
`9c40e30baf4b5ab66dac07522adef4192d9a2e19`.

License source: <https://github.com/Thealexbarney/VGAudio/blob/9c40e30baf4b5ab66dac07522adef4192d9a2e19/LICENSE>

The VGAudio 2.2.1 NuGet metadata identifies Alex Barney as author and owner and
states `Copyright © 2016 Alex Barney`. The pinned source's `LICENSE` has the
copyright line `Copyright (c) 2016` without a name. The following reproduces
that upstream license verbatim:

```text
The MIT License (MIT)

Copyright (c) 2016

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## muscriptor.cpp

Use: statically linked C++ inference for MuScriptor audio transcription.

Upstream: <https://github.com/DamRsn/muscriptor.cpp>

```text
MIT License

Copyright (c) 2026 Damien Ronssin

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

--------------------------------------------------------------------------------

This project is a port of MuScriptor (https://github.com/muscriptor/muscriptor),
whose code is distributed under the following licence:

MIT License

Copyright (c) 2026 Kyutai x Mirelo

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

--------------------------------------------------------------------------------

Not covered by the licences above:

- cpp/third_party/pffft keeps its own BSD-style licence, in the header of
  cpp/third_party/pffft/pffft.c.
- testdata/audio holds an excerpt of a recording licensed CC BY 4.0; see
  testdata/audio/README.md.
- The MuScriptor model weights are licensed CC BY-NC 4.0 by their authors. No
  weights are included in this repository.
```

## pffft

Use: FFT routines vendored in muscriptor.cpp and statically linked.

Upstream: <https://bitbucket.org/jpommier/pffft>

```text
Copyright (c) 2013  Julien Pommier ( pommier@modartt.com )

   Based on original fortran 77 code from FFTPACKv4 from NETLIB
   (http://www.netlib.org/fftpack), authored by Dr Paul Swarztrauber
   of NCAR, in 1985.

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


   PFFFT : a Pretty Fast FFT.

   This file is largerly based on the original FFTPACK implementation, modified in
   order to take advantage of SIMD instructions of modern CPUs.
```

## ggml

Use: statically linked tensor library (CPU backend) used by muscriptor.cpp.

Upstream: <https://github.com/ggml-org/ggml>

```text
MIT License

Copyright (c) 2023-2026 The ggml authors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## MuScriptor model weights

Use: `muscriptor-small-f16.gguf`, shipped beside the ASI and read only to
transcribe songs that have no `song.mid`. Converted to GGUF by
<https://huggingface.co/DamRsn/muscriptor-gguf> (revision
`d7045f94e8b19427f4ff9542975035e66596e51c`).

Licence: CC BY-NC 4.0, non-commercial use only,
<https://creativecommons.org/licenses/by-nc/4.0/legalcode.en>. The notice below
is reproduced from the converted model repository.

### Attribution and conditions of use

#### Original work

**MuScriptor** — a multi-instrument automatic music transcription model by
**Mirelo** and **Kyutai**.

- Weights: <https://huggingface.co/MuScriptor>
- Code: <https://github.com/muscriptor/muscriptor>
- Paper: <https://arxiv.org/abs/2607.08168v1>
- Licence: CC BY-NC 4.0, <https://creativecommons.org/licenses/by-nc/4.0/>

This repository is not affiliated with or endorsed by Mirelo or Kyutai.

#### Changes made

The original safetensors weights were converted to GGUF and the bulk weights
cast to float16. No retraining, fine-tuning or pruning; no tensor values altered
beyond that cast. The converter is in
[`muscriptor.cpp`](https://github.com/DamRsn/muscriptor.cpp).

#### Upstream conditions of use

Mirelo and Kyutai supplement CC BY-NC 4.0 with the following, reproduced
verbatim from the upstream model cards. It is quoted for information: this
repository imposes no terms of its own, and grants these files under
CC BY-NC 4.0 alone.

> MuScriptor is the result of a research collaboration between Mirelo and Kyutai
> whose purpose is to transcribe audio to MIDI/music sheet. It is provided
> primarily for research purposes under the CC BY-NC 4.0 licence supplemented by
> the below specific conditions of use.
>
> Specific conditions of use: MuScriptor and any generated content by MuScriptor
> are provided as is without any warranty of any kind, including but not limited
> to any warranty of non-infringement. Use of MuScriptor and its output must
> comply with all applicable laws and must not result in, involve, or facilitate
> any illegal or unauthorized activity. Prohibited uses include, without
> limitation, inputting music files and transcribing them to MIDI/music sheet
> without having all the necessary rights, including intellectual property
> rights, under applicable laws. Accordingly, users of MuScriptor undertake and
> warrant to have all the necessary rights, including intellectual property
> rights, in connection with their use of MuScriptor and its output. We disclaim
> all liability for any non-compliant use and users of MuScriptor shall
> indemnify, defend, and hold harmless Mirelo and Kyutai from and against any
> and all claims, damages, losses, liabilities, and expenses (including
> reasonable attorneys' fees) incurred by Mirelo and/or Kyutai arising out of or
> resulting from their failure to comply with the terms of the CC BY-NC 4.0
> licence and/or these specific conditions of use.

The authoritative text is on the upstream model cards. Where this copy and
theirs differ, theirs governs.
