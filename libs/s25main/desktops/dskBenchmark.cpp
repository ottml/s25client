// Copyright (C) 2005 - 2025 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "dskBenchmark.h"
#include "Game.h"
#include "Loader.h"
#include "PlayerInfo.h"
#include "RttrForeachPt.h"
#include "WindowManager.h"
#include "buildings/nobMilitary.h"
#include "controls/ctrlText.h"
#include "drivers/VideoDriverWrapper.h"
#include "dskMainMenu.h"
#include "factories/BuildingFactory.h"
#include "figures/nofPassiveSoldier.h"
#include "figures/nofPassiveWorker.h"
#include "helpers/mathFuncs.h"
#include "helpers/toString.h"
#include "lua/GameDataLoader.h"
#include "ogl/FontStyle.h"
#include "ogl/IRenderer.h"
#include "random/Random.h"
#include "world/GameWorld.h"
#include "world/GameWorldView.h"
#include "world/GameWorldViewer.h"
#include "world/MapLoader.h"
#include "gameTypes/RoadBuildState.h"
#include "gameData/GameLoader.h"
#include "s25util/Log.h"
#include "s25util/strFuncs.h"
#include <helpers/chronoIO.h>
#include <memory>
#include <random>

namespace {
enum
{
    ID_txtHelp = dskMenuBase::ID_FIRST_FREE,
    ID_txtAmount,
    ID_first
};
}

static const unsigned numTestFrames = 500u;

struct dskBenchmark::GameView
{
    GameWorldViewer viewer;
    GameWorldView view;
    GameView(GameWorldBase& gw, Extent size) : viewer(0u, gw), view(viewer, Position(0, 0), size)
    {
        viewer.InitTerrainRenderer();
        view.MoveToMapPt(MapPoint(0, 0));
        view.ToggleShowBQ();
        view.ToggleShowNames();
    }
};

dskBenchmark::dskBenchmark()
    : curTest_(Benchmark::None), runAll_(false), numInstances_(1000), frameCtr_(FrameCounter::clock::duration::max())
{
    for(std::chrono::milliseconds& t : testDurations_)
        t = std::chrono::milliseconds::zero();
    AddText(ID_txtHelp, DrawPoint(5, 5), "Use F1-F5 to start benchmark, F10 for all, NUM_n to set amount of instances",
            COLOR_YELLOW, FontStyle::LEFT, LargeFont);
    AddText(ID_txtAmount, DrawPoint(795, 5), "Instances: default", COLOR_YELLOW, FontStyle::RIGHT, LargeFont);
}

dskBenchmark::~dskBenchmark()
{
    try
    {
        printTimes();
    } catch(...)
    {}
}

bool dskBenchmark::Msg_KeyDown(const KeyEvent& ke)
{
    switch(ke.kt)
    {
        case KeyType::Escape: WINDOWMANAGER.Switch(std::make_unique<dskMainMenu>()); break;
        case KeyType::F1: startTest(Benchmark::Text); break;
        case KeyType::F2: startTest(Benchmark::Primitives); break;
        case KeyType::F3: startTest(Benchmark::EmptyGame); break;
        case KeyType::F4: startTest(Benchmark::BasicGame); break;
        case KeyType::F5: startTest(Benchmark::FullGame); break;
        case KeyType::F10:
            runAll_ = true;
            startTest(Benchmark::Text);
            break;
        case KeyType::Char:
            if(ke.c >= '0' && ke.c <= '9')
            {
                numInstances_ = (ke.c - '0') * 100;
                if(numInstances_ == 0)
                    numInstances_ = 1000;
                GetCtrl<ctrlText>(ID_txtAmount)->SetText("Instances: " + helpers::toString(numInstances_));
                break;
            } else
                return dskMenuBase::Msg_KeyDown(ke);
        default: return dskMenuBase::Msg_KeyDown(ke);
    }
    return true;
}

void dskBenchmark::Msg_PaintAfter()
{
    for(const ColoredRect& rect : rects_)
        DrawRectangle(rect.rect, rect.clr);
    for(const ColoredLine& line : lines_)
        DrawLine(line.p1, line.p2, line.width, line.clr);
    if(gameView_)
    {
        RoadBuildState roadState;
        roadState.mode = RoadBuildMode::Disabled;
        gameView_->view.Draw(roadState, MapPoint::Invalid(), false);
    }
    if(curTest_ != Benchmark::None)
    {
        if(frameCtr_.getCurNumFrames() + 1u >= numTestFrames)
            VIDEODRIVER.GetRenderer()->synchronize();
        frameCtr_.update();
        if(frameCtr_.getCurNumFrames() >= numTestFrames)
            finishTest();
    }
    dskMenuBase::Msg_PaintAfter();
}

void dskBenchmark::SetActive(bool activate)
{
    if(!IsActive() && activate)
        VIDEODRIVER.ResizeScreen(VideoMode(1600, 900), false);
    dskMenuBase::SetActive(activate);
}

void dskBenchmark::startTest(Benchmark test)
{
    uint32_t seed = 0x1337;
    std::mt19937 rng(seed);
    switch(test)
    {
        case Benchmark::None: return;
        case Benchmark::Text:
        {
            static const std::string charset =
              "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*()`~-_=+[{]{\\|;:'\",<.>/? ";

            std::uniform_int_distribution<unsigned> distr(1, 30);
            std::uniform_int_distribution<unsigned> distr2(0, 100000);
            std::uniform_int_distribution<unsigned> distrMove(10, 25);
            DrawPoint pt(0, 0);
            const glFont* fnt = NormalFont;
            for(int i = 0; i < numInstances_; i++)
            {
                std::string txt = createRandString(distr(rng), charset, seed);
                seed += distr2(rng);
                pt.y += distrMove(rng);
                if(pt.y >= 580)
                {
                    pt.y = 0;
                    pt.x += 150 + distrMove(rng) / 3;
                    if(pt.x >= 780)
                        pt.x = distrMove(rng);
                }
                AddText(ID_first + i, pt, txt, COLOR_YELLOW, FontStyle::LEFT, fnt);
            }
            break;
        }
        case Benchmark::Primitives:
        {
            Extent screenSize = VIDEODRIVER.GetRenderSize();
            std::uniform_int_distribution<unsigned> distSize(5, 50);
            std::uniform_int_distribution<unsigned> distClr(0, 0xFF);
            std::uniform_int_distribution<unsigned> distrMove(10, 50);
            std::uniform_int_distribution<unsigned> distrPosX(0, screenSize.x);
            std::uniform_int_distribution<unsigned> distrPosY(0, screenSize.y);
            std::uniform_int_distribution<unsigned> distrWidth(1, 10);
            DrawPoint pt(0, 0);
            for(int i = 0; i < numInstances_; i++)
            {
                ColoredRect rect;
                rect.rect.move(pt);
                rect.rect.setSize(Extent(distSize(rng), distSize(rng)));
                unsigned clr = distClr(rng);
                unsigned alpha = std::min((distClr(rng) + 10u) * 10u, 0xFFu);
                rect.clr = MakeColor(alpha, clr, clr, clr);
                rects_.push_back(rect);
                pt.y += distrMove(rng);
                if(pt.y >= static_cast<int>(screenSize.y) - 20)
                {
                    pt.y = 0;
                    pt.x += 150 + distrMove(rng) / 3;
                    if(pt.x >= static_cast<int>(screenSize.x) - 20)
                        pt.x = distrMove(rng);
                }
                ColoredLine line;
                line.p1 = Position(distrPosX(rng), distrPosY(rng));
                line.p2 = Position(distrPosX(rng), distrPosY(rng));
                line.width = distrWidth(rng);
                line.clr = MakeColor(alpha, clr, clr, clr);
                lines_.push_back(line);
            }
            break;
        }
        case Benchmark::EmptyGame:
            createGame();
            if(!game_)
                return;
            RTTR_FOREACH_PT(MapPoint, game_->world_.GetSize())
            {
                game_->world_.SetVisibility(pt, 0, Visibility::Visible);
            }
            break;
        case Benchmark::BasicGame:
        {
            createGame();
            if(!game_)
                return;
            std::vector<MapPoint> hqs(2, MapPoint(0, 0));
            hqs[1].x += 30;
            MapLoader::PlaceHQs(game_->world_, hqs, false);
            break;
        }
        case Benchmark::FullGame:
        {
            createGame();
            if(!game_)
                return;
            std::vector<MapPoint> hqs(2, MapPoint(0, 0));
            hqs[1].x += 30;
            MapLoader::PlaceHQs(game_->world_, hqs, false);
            for(unsigned i = 0; i < hqs.size(); i++)
            {
                std::vector<MapPoint> pts = game_->world_.GetPointsInRadius(hqs[i], 15);
                std::bernoulli_distribution dist(numInstances_ / 1000.f);
                std::bernoulli_distribution distEqual;
                std::array<BuildingType, 5> blds = {{BuildingType::Barracks, BuildingType::Mill, BuildingType::IronMine,
                                                     BuildingType::Slaughterhouse, BuildingType::Bakery}};
                std::uniform_int_distribution<unsigned> getBld(0, blds.size() - 1);
                std::uniform_int_distribution<unsigned> getJob(0, helpers::MaxEnumValue_v<Job>);
                std::uniform_int_distribution<unsigned> getDir(0, helpers::MaxEnumValue_v<Direction>);
                for(MapPoint pt : pts)
                {
                    MapPoint flagPt = game_->world_.GetNeighbour(pt, Direction::SouthEast);
                    if(game_->world_.GetNode(pt).obj || game_->world_.GetNode(flagPt).obj || !dist(rng))
                        continue;
                    BuildingType bldType = blds[getBld(rng)];
                    noBuilding* bld = BuildingFactory::CreateBuilding(
                      game_->world_, bldType, pt, i, distEqual(rng) ? Nation::Africans : Nation::Japanese);
                    if(bldType == BuildingType::Barracks)
                    {
                        auto* mil = static_cast<nobMilitary*>(bld);
                        mil->AddPassiveSoldier(std::make_unique<nofPassiveSoldier>(pt, i, mil, mil, 0));
                    }
                    auto& figure = game_->world_.AddFigure(
                      flagPt, std::make_unique<nofPassiveWorker>(Job(getJob(rng)), flagPt, i, nullptr));
                    figure.StartWandering();
                    figure.StartWalking(Direction(getDir(rng)));
                }
            }
            break;
        }
    }
    if(game_)
        gameView_ = std::make_unique<GameView>(game_->world_, VIDEODRIVER.GetRenderSize());
    VIDEODRIVER.GetRenderer()->synchronize();
    VIDEODRIVER.setTargetFramerate(-1);
    curTest_ = test;
    frameCtr_ = FrameCounter(frameCtr_.getUpdateInterval());
}

void dskBenchmark::finishTest()
{
    using namespace std::chrono;
    using helpers::withUnit;
    LOG.write("Benchmark #%1% took %2%. -> %3%m/frame\n") % rttr::enum_cast(curTest_)
      % withUnit(duration_cast<duration<float>>(frameCtr_.getCurIntervalLength()))
      % withUnit(duration_cast<milliseconds>(frameCtr_.getCurIntervalLength() / frameCtr_.getCurNumFrames()));
    if(testDurations_[curTest_] == milliseconds::zero())
        testDurations_[curTest_] = duration_cast<milliseconds>(frameCtr_.getCurIntervalLength());
    else
        testDurations_[curTest_] =
          duration_cast<milliseconds>(testDurations_[curTest_] + frameCtr_.getCurIntervalLength()) / 2;

    std::vector<Window*> ctrls = GetCtrls<Window>();
    for(Window* ctrl : ctrls)
    {
        if(ctrl->GetID() >= ID_first)
            DeleteCtrl(ctrl->GetID());
    }
    rects_.clear();
    lines_.clear();
    gameView_.reset();
    game_.reset();
    SetFpsDisplay(true);
    VIDEODRIVER.setTargetFramerate(0);
    if(!runAll_)
        curTest_ = Benchmark::None;
    else
    {
        if(curTest_ == helpers::MaxEnumerator_v<Benchmark>)
            curTest_ = Benchmark::None;
        else
        {
            curTest_ = Benchmark(rttr::enum_cast(curTest_) + 1);
            startTest(curTest_);
        }
    }
}

void dskBenchmark::createGame()
{
    RANDOM.Init(42);
    std::vector<PlayerInfo> players;
    PlayerInfo p;
    p.ps = PlayerState::Occupied;
    p.nation = Nation::Africans;
    p.color = PLAYER_COLORS[0];
    players.push_back(p);
    p.nation = Nation::Japanese;
    p.color = PLAYER_COLORS[1];
    players.push_back(p);
    game_ = std::make_shared<Game>(GlobalGameSettings(), 0u, players);
    GameWorld& world = game_->world_;
    try
    {
        loadGameData(world.GetDescriptionWriteable());
        world.Init(MapExtent(128, 128));
        const WorldDescription& desc = world.GetDescription();
        DescIdx<TerrainDesc> lastTerrain(0);
        int lastHeight = 10;
        std::mt19937 rng(42);
        using std::uniform_int_distribution;
        uniform_int_distribution<int> percentage(0, 100);
        uniform_int_distribution<int> randTerrain(0, desc.terrain.size() / 2);
        RTTR_FOREACH_PT(MapPoint, world.GetSize())
        {
            MapNode& node = world.GetNodeWriteable(pt);
            DescIdx<TerrainDesc> t;
            // 90% chance of using the same terrain
            if(percentage(rng) <= 90)
                t = lastTerrain;
            else
                t.value = randTerrain(rng);
            node.t1 = t;
            lastTerrain = t;
            if(percentage(rng) <= 90)
                t = lastTerrain;
            else
                t.value = randTerrain(rng);
            node.t2 = t;
            lastTerrain = t;
            if(percentage(rng) <= 70)
                lastHeight = helpers::clamp(lastHeight + uniform_int_distribution<int>(-1, 1)(rng), 8, 13);
            node.altitude = lastHeight;
        }
        MapLoader::InitShadows(world);
        MapLoader::SetMapExplored(world);

        GameLoader loader(LOADER, game_);
        if(!loader.load())
            throw "GUI";
    } catch(...)
    {
        game_.reset();
    }
}

void dskBenchmark::printTimes() const
{
    using namespace std::chrono;

    milliseconds total(0);
    for(const auto i : helpers::enumRange<Benchmark>())
    {
        if(i == Benchmark::None)
            continue;
        LOG.write("Benchmark #%1% took %2% -> %3%/frame\n") % rttr::enum_cast(i)
          % helpers::withUnit(duration_cast<duration<float>>(testDurations_[i]))
          % helpers::withUnit(duration_cast<milliseconds>(testDurations_[i] / numTestFrames));
        total += testDurations_[i];
    }
    LOG.write("Total benchmark time; %1% -> %2%/frame\n") % helpers::withUnit(duration_cast<duration<float>>(total))
      % helpers::withUnit(duration_cast<milliseconds>(total / numTestFrames));
}
