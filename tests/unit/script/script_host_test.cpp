#include <catch2/catch_test_macros.hpp>

#include "engine/script/script_host.hpp"
#include "modules/script_python/python_script_engine.hpp"

using mosaic::engine::script::ScriptHost;
using mosaic::script::python::create_python_script_engine;

TEST_CASE("bind_host exposes play/pause/stop to scripts", "[script]")
{
    int  playing_calls = 0;
    bool last_playing  = false;
    int  stop_calls    = 0;

    ScriptHost host;
    host.set_playing  = [&](bool p) { ++playing_calls; last_playing = p; };
    host.request_stop = [&] { ++stop_calls; };

    auto engine = create_python_script_engine();
    engine->bind_host(host);

    REQUIRE(engine->run_string("pause()").ok);
    REQUIRE(playing_calls == 1);
    REQUIRE(last_playing == false);

    REQUIRE(engine->run_string("play()").ok);
    REQUIRE(playing_calls == 2);
    REQUIRE(last_playing == true);

    REQUIRE(engine->run_string("stop()").ok);
    REQUIRE(stop_calls == 1);
}

TEST_CASE("bind_host with empty callbacks is a safe no-op", "[script]")
{
    auto engine = create_python_script_engine();
    engine->bind_host(ScriptHost{}); // both callbacks empty
    REQUIRE(engine->run_string("stop()\npause()\nplay()").ok); // no crash, no throw
}
