// Copyright Contributors to the Open Shading Language project.
// SPDX-License-Identifier: BSD-3-Clause
// https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

#include <BSDL/SPI/bsdf_diffuse_impl.h>
#include <BSDL/bsdf_impl.h>

#include <cassert>

namespace {

struct TestRoot {
    template<typename T>
    TestRoot(T*, float roughness, float lambda_0, bool transmissive)
        : m_roughness(roughness)
        , m_lambda_0(lambda_0)
        , m_transmissive(transmissive)
    {
    }

    void set_roughness(float roughness) { m_roughness = roughness; }
    float roughness() const { return m_roughness; }
    float lambda_0() const { return m_lambda_0; }
    bool transmissive() const { return m_transmissive; }

private:
    float m_roughness;
    float m_lambda_0;
    bool m_transmissive;
};

}  // namespace

int
main()
{
    const Imath::V3f normal(0.0f, 0.0f, 1.0f);
    const bsdl::BsdfGlobals globals(normal, normal, normal, false, 0.0f, 1.0f,
                                    0.0f);

    using DiffuseLobe = bsdl::spi::DiffuseLobe<TestRoot>;
    DiffuseLobe::Data data = { normal };
    DiffuseLobe lobe(static_cast<DiffuseLobe*>(nullptr), globals, data);

    const bsdl::Sample eval = lobe.eval_impl(globals.wo, normal);
    assert(!eval.null());
    assert(eval.pdf > 0.0f);
    assert(eval.weight.max() > 0.0f);

    const bsdl::Sample sample
        = lobe.sample_impl(globals.wo, Imath::V3f(0.25f, 0.5f, 0.75f));
    assert(!sample.null());
    assert(sample.pdf > 0.0f);
    assert(sample.weight.max() > 0.0f);

    return 0;
}
