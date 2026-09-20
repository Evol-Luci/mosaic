#include <catch2/catch_test_macros.hpp>

#include "studio/scene_file.hpp"

using mosaic::studio::SceneFileModel;

TEST_CASE("SceneFileModel tracks path, dirty, and a one-shot save request", "[studio]")
{
    SceneFileModel m("scene.json");
    REQUIRE(m.path() == "scene.json");
    REQUIRE_FALSE(m.dirty());

    m.mark_dirty();
    REQUIRE(m.dirty());

    REQUIRE_FALSE(m.consume_save_request()); // none pending
    m.request_save();
    REQUIRE(m.consume_save_request());       // one-shot
    REQUIRE_FALSE(m.consume_save_request()); // cleared after consume

    m.mark_clean();
    REQUIRE_FALSE(m.dirty());
}

TEST_CASE("SceneFileModel's reload request is one-shot and independent of the save request",
          "[studio]")
{
    SceneFileModel m("scene.json");

    REQUIRE_FALSE(m.consume_reload_request()); // none pending
    m.request_reload();
    REQUIRE(m.consume_reload_request());       // one-shot
    REQUIRE_FALSE(m.consume_reload_request()); // cleared after consume

    m.request_save();
    REQUIRE_FALSE(m.consume_reload_request()); // save request doesn't leak into reload
    REQUIRE(m.consume_save_request());

    m.request_reload();
    REQUIRE_FALSE(m.consume_save_request());   // reload request doesn't leak into save
    REQUIRE(m.consume_reload_request());
}
