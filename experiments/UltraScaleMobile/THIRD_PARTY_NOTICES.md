Third-party shader notice

Snapdragon Game Super Resolution 1 (SGSR1)
- Source: https://github.com/SnapdragonGameStudios/snapdragon-gsr
- Shader: sgsr/v1/include/glsl/sgsr1_shader_mobile.frag
- Copyright (c) 2025, Qualcomm Innovation Center, Inc.
- License: BSD 3-Clause. The upstream copyright notice, conditions and disclaimer are reproduced below.

Copyright (c) 2023, Qualcomm Innovation Center, Inc. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice,
   this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors
   may be used to endorse or promote products derived from this software
   without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

SPDX-License-Identifier: BSD-3-Clause

MoonlightWeb Super Resolution (MWSR)
- Source: https://github.com/linckosz/moonlight-web/tree/main/mwsr
- Shader: mwsr/glsl/mwsr-up.frag (GLSL ES 3.00), upstream shader retained with its copyright and MIT notice.
- Copyright (c) 2026 Bruno Martin.
- License: MIT. The shader file contains the full upstream notice.

AMD FidelityFX Contrast Adaptive Sharpening (CAS)
- Source: https://github.com/GPUOpen-Effects/FidelityFX-CAS
- Reference implementation: ffx-cas/ffx_cas.h, sharpen-only (noScaling) path.
- Fragment shader in this project is a GLES adaptation of the official CAS filter math; it uses texelFetch on a 3x3 neighbourhood and the default shared-green-coefficient path.
- Copyright (c) 2017-2020 Advanced Micro Devices, Inc.
- License: MIT. The upstream license text is reproduced below.

Copyright (c) 2020 Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
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

SPDX-License-Identifier: MIT


AMD FidelityFX Super Resolution 1 (EASU)
- Source: https://github.com/GPUOpen-Effects/FidelityFX-FSR
- Shader: `app/src/main/assets/upscalers/amd_fsr1_easu.frag` and `project/sample-game/src/main/assets/shaders/amd_fsr1_easu.frag`.
- Copyright (c) 2021 Advanced Micro Devices, Inc.
- License: MIT. The complete copyright and permission notice is included at the top of each extracted shader asset.
- The sample-game copy adapts the shader interface from GLSL ES 1.00-style syntax to GLSL ES 3.00 (varying/input, texture sampling and fragment output names); the EASU reconstruction math is retained.


Snapdragon Game Super Resolution 2 (SGSR2)
- Source: https://github.com/SnapdragonGameStudios/snapdragon-gsr/tree/main/sgsr/v2
- Bundled Android app assets: `sgsr2_2pass_convert.frag`, `sgsr2_2pass_upscale.frag`, and `sgsr2_2pass_vertex.vert` (the upstream Vulkan GLSL ES 3.20 two-pass fragment variant).
- Copyright (c) 2024, Qualcomm Innovation Center, Inc.
- License: BSD-3-Clause. The upstream shader files retain their SPDX identifier; the full BSD-3-Clause text is reproduced in the SGSR1 notice above and also available in the upstream repository LICENSE.
- These are reference shader assets only until a Vulkan pipeline supplies the required depth, motion-vector, camera/jitter and temporal-history resources.
