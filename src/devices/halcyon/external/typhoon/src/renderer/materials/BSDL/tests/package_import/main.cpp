// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

#include <BSDL/SPI/bsdf_diffuse_impl.h>
#include <BSDL/bsdf_impl.h>
#include <BSDL/microfacet_tools_impl.h>

int
main()
{
    return bsdl::Power::UNIT().max() > 0.0f ? 0 : 1;
}
