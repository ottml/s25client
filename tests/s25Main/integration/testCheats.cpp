// Copyright (C) 2024 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Cheats.h"
#include "GamePlayer.h"
#include "desktops/dskGameInterface.h"
#include "worldFixtures/CreateEmptyWorld.h"
#include "worldFixtures/WorldFixture.h"
#include <turtle/mock.hpp>

BOOST_AUTO_TEST_SUITE(CheatsTests)

namespace {
constexpr auto numPlayers = 1;
constexpr auto worldWidth = 64;
constexpr auto worldHeight = 64;
struct CheatsFixture : WorldFixture<CreateEmptyWorld, numPlayers, worldWidth, worldHeight>
{
    CheatsFixture()
        : cheats{gameDesktop.GI_GetCheats()}, viewer{gameDesktop.GetView().GetViewer()}, p1HQPos{p1.GetHQPos()}
    {}

    dskGameInterface gameDesktop{game, nullptr, 0, false};
    Cheats& cheats;
    const GameWorldViewer& viewer;

    GamePlayer& getPlayer(unsigned id) { return world.GetPlayer(id); }
    GamePlayer& p1 = getPlayer(0);
    const MapPoint p1HQPos;
};
} // namespace

BOOST_FIXTURE_TEST_CASE(CanToggleCheatModeOnAndOffRepeatedly, CheatsFixture)
{
    BOOST_TEST_REQUIRE(cheats.isCheatModeOn() == false); // initially off
    cheats.toggleCheatMode();
    BOOST_TEST_REQUIRE(cheats.isCheatModeOn() == true);
    cheats.toggleCheatMode();
    BOOST_TEST_REQUIRE(cheats.isCheatModeOn() == false);
    cheats.toggleCheatMode();
    BOOST_TEST_REQUIRE(cheats.isCheatModeOn() == true);
    cheats.toggleCheatMode();
    BOOST_TEST_REQUIRE(cheats.isCheatModeOn() == false);
}

BOOST_FIXTURE_TEST_CASE(TurningCheatModeOffDisablesAllCheats, CheatsFixture)
{
    cheats.toggleCheatMode();
    cheats.toggleAllVisible();
    BOOST_TEST_REQUIRE(cheats.isAllVisible() == true);
    cheats.toggleAllBuildingsEnabled();
    BOOST_TEST_REQUIRE(cheats.areAllBuildingsEnabled() == true);
    cheats.toggleCheatMode();
    BOOST_TEST_REQUIRE(cheats.isAllVisible() == false);
    BOOST_TEST_REQUIRE(cheats.areAllBuildingsEnabled() == false);
    // testing toggleHumanAIPlayer would require GameClient::state==Loaded, which is guaranteed in code (because Cheats
    // only exist after the game is loaded) but is not the case in tests - skipping
}

namespace {
MOCK_BASE_CLASS(MockGameInterface, GameInterface)
{
    // LCOV_EXCL_START
    MOCK_METHOD(GI_PlayerDefeated, 1)
    MOCK_METHOD(GI_UpdateMinimap, 1)
    MOCK_METHOD(GI_FlagDestroyed, 1)
    MOCK_METHOD(GI_TreatyOfAllianceChanged, 1)
    MOCK_METHOD(GI_UpdateMapVisibility, 0)
    MOCK_METHOD(GI_Winner, 1)
    MOCK_METHOD(GI_TeamWinner, 1)
    MOCK_METHOD(GI_StartRoadBuilding, 2)
    MOCK_METHOD(GI_CancelRoadBuilding, 0)
    MOCK_METHOD(GI_BuildRoad, 0)
    // clang-format off
    MOCK_METHOD(GI_GetCheats, 0, Cheats&(void))
    // clang-format on
    // LCOV_EXCL_STOP
};
} // namespace

BOOST_FIXTURE_TEST_CASE(CanToggleAllVisible_IfCheatModeIsOn, CheatsFixture)
{
    MockGameInterface mgi;
    MOCK_EXPECT(mgi.GI_GetCheats).returns(std::ref(gameDesktop.GI_GetCheats()));
    MOCK_EXPECT(mgi.GI_UpdateMapVisibility).exactly(3); // how many toggles should actually occur
    world.SetGameInterface(&mgi);

    MapPoint farawayPos = p1HQPos;
    farawayPos.x += 20;

    // initially farawayPos is not visible
    BOOST_TEST_REQUIRE((viewer.GetVisibility(farawayPos) == Visibility::Visible) == false);

    cheats.toggleAllVisible();
    // still not visible - cheat mode is not on
    BOOST_TEST_REQUIRE((viewer.GetVisibility(farawayPos) == Visibility::Visible) == false);

    cheats.toggleCheatMode();
    cheats.toggleAllVisible();
    // now visible - cheat mode is on
    BOOST_TEST_REQUIRE((viewer.GetVisibility(farawayPos) == Visibility::Visible) == true);

    cheats.toggleAllVisible();
    BOOST_TEST_REQUIRE((viewer.GetVisibility(farawayPos) == Visibility::Visible) == false);
    cheats.toggleAllVisible();
    BOOST_TEST_REQUIRE((viewer.GetVisibility(farawayPos) == Visibility::Visible) == true);
}

BOOST_FIXTURE_TEST_CASE(CanToggleAllBuildingsEnabled_IfCheatModeIsOn, CheatsFixture)
{
    BOOST_TEST_REQUIRE(cheats.areAllBuildingsEnabled() == false);
    cheats.toggleAllBuildingsEnabled();
    BOOST_TEST_REQUIRE(cheats.areAllBuildingsEnabled() == false);
    cheats.toggleCheatMode();
    cheats.toggleAllBuildingsEnabled();
    BOOST_TEST_REQUIRE(cheats.areAllBuildingsEnabled() == true);
    cheats.toggleAllBuildingsEnabled();
    BOOST_TEST_REQUIRE(cheats.areAllBuildingsEnabled() == false);
    cheats.toggleAllBuildingsEnabled();
    BOOST_TEST_REQUIRE(cheats.areAllBuildingsEnabled() == true);
}

BOOST_AUTO_TEST_SUITE_END()
