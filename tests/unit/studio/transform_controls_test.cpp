#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cmath>

#include "studio/transform_controls.hpp"

using namespace mosaic::studio;
using mosaic::foundation::math::Quat;
using mosaic::foundation::math::from_axis_angle;
using mosaic::foundation::math::Vec3;

namespace
{
constexpr float kPi = 3.14159265358979323846f;
void approx_quat(const Quat& a, const Quat& b, float eps = 1e-4f)
{
    // Quats double-cover: q and -q are the same rotation.
    const float s = (a.w * b.w < 0.0f) ? -1.0f : 1.0f;
    REQUIRE_THAT(a.x, Catch::Matchers::WithinAbs(s * b.x, eps));
    REQUIRE_THAT(a.y, Catch::Matchers::WithinAbs(s * b.y, eps));
    REQUIRE_THAT(a.z, Catch::Matchers::WithinAbs(s * b.z, eps));
    REQUIRE_THAT(a.w, Catch::Matchers::WithinAbs(s * b.w, eps));
}
} // namespace

TEST_CASE("euler_to_quat of zero is identity")
{
    approx_quat(euler_to_quat(EulerAngles{0.0f, 0.0f, 0.0f}), Quat::identity());
}

TEST_CASE("euler_to_quat yaw matches Y axis-angle")
{
    approx_quat(euler_to_quat(EulerAngles{0.0f, kPi * 0.5f, 0.0f}),
                from_axis_angle(Vec3{0.0f, 1.0f, 0.0f}, kPi * 0.5f));
}

TEST_CASE("euler/quat round trips for non-singular angles")
{
    for (const EulerAngles e : {EulerAngles{0.3f, -0.7f, 1.1f},
                                EulerAngles{-0.5f, 0.9f, -0.2f}})
    {
        const EulerAngles r = quat_to_euler(euler_to_quat(e));
        REQUIRE_THAT(r.pitch, Catch::Matchers::WithinAbs(e.pitch, 1e-4f));
        REQUIRE_THAT(r.yaw, Catch::Matchers::WithinAbs(e.yaw, 1e-4f));
        REQUIRE_THAT(r.roll, Catch::Matchers::WithinAbs(e.roll, 1e-4f));
    }
}

TEST_CASE("controls model reseeds euler only when entity changes")
{
    using mosaic::foundation::EntityID;
    TransformControlsModel m;
    const EntityID e1{1};
    const EntityID e2{2};

    m.sync(e1, euler_to_quat(EulerAngles{0.2f, 0.0f, 0.0f}));
    REQUIRE_THAT(m.euler.pitch, Catch::Matchers::WithinAbs(0.2f, 1e-4f));

    // Same entity, different stored quat: euler must NOT be clobbered mid-edit.
    m.euler.pitch = 0.9f;
    m.sync(e1, euler_to_quat(EulerAngles{0.0f, 0.0f, 0.0f}));
    REQUIRE_THAT(m.euler.pitch, Catch::Matchers::WithinAbs(0.9f, 1e-4f));

    // New entity: reseed from its stored quat.
    m.sync(e2, euler_to_quat(EulerAngles{-0.4f, 0.0f, 0.0f}));
    REQUIRE_THAT(m.euler.pitch, Catch::Matchers::WithinAbs(-0.4f, 1e-4f));
}
