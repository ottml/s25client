// Copyright (C) 2005 - 2024 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "nobHarborBuilding.h"
#include "EventManager.h"
#include "GamePlayer.h"
#include "GlobalGameSettings.h"
#include "Loader.h"
#include "SerializedGameData.h"
#include "Ware.h"
#include "figures/noFigure.h"
#include "figures/nofAttacker.h"
#include "figures/nofDefender.h"
#include "helpers/containerUtils.h"
#include "helpers/pointerContainerUtils.h"
#include "network/GameClient.h"
#include "nobMilitary.h"
#include "ogl/glArchivItem_Bitmap.h"
#include "ogl/glArchivItem_Bitmap_Player.h"
#include "ogl/glSmartBitmap.h"
#include "pathfinding/RoadPathFinder.h"
#include "postSystem/PostMsgWithBuilding.h"
#include "random/Random.h"
#include "world/GameWorld.h"
#include "nodeObjs/noShip.h"
#include "gameData/BuildingConsts.h"
#include "gameData/GameConsts.h"
#include "gameData/MilitaryConsts.h"
#include "gameData/ShieldConsts.h"

nobHarborBuilding::ExpeditionInfo::ExpeditionInfo(SerializedGameData& sgd)
    : boards(sgd.PopUnsignedInt()), stones(sgd.PopUnsignedInt()), active(sgd.PopBool()), builder(sgd.PopBool())
{}

void nobHarborBuilding::ExpeditionInfo::Serialize(SerializedGameData& sgd) const
{
    sgd.PushUnsignedInt(boards);
    sgd.PushUnsignedInt(stones);
    sgd.PushBool(active);
    sgd.PushBool(builder);
}

nobHarborBuilding::ExplorationExpeditionInfo::ExplorationExpeditionInfo(SerializedGameData& sgd)
    : active(sgd.PopBool()), scouts(sgd.PopUnsignedInt())
{}

void nobHarborBuilding::ExplorationExpeditionInfo::Serialize(SerializedGameData& sgd) const
{
    sgd.PushBool(active);
    sgd.PushUnsignedInt(scouts);
}

nobHarborBuilding::nobHarborBuilding(const MapPoint pos, const unsigned char player, const Nation nation)
    : nobBaseWarehouse(BuildingType::HarborBuilding, pos, player, nation), orderware_ev(nullptr)
{
    // ins Militärquadrat einfügen
    world->GetMilitarySquares().Add(this);
    world->RecalcTerritory(*this, TerritoryChangeReason::Build);

    // Alle Waren 0
    inventory.clear();

    // Aktuellen Warenbestand zur aktuellen Inventur dazu addieren
    AddToInventory();

    // Take 1 as the reserve per rank
    for(unsigned i = 0; i <= world->GetGGS().GetMaxMilitaryRank(); ++i)
    {
        reserve_soldiers_claimed_visual[i] = reserve_soldiers_claimed_real[i] = 1;
        RefreshReserve(i);
    }

    /// Die Meere herausfinden, an die dieser Hafen grenzt
    for(const auto dir : helpers::EnumRange<Direction>{})
        seaIds[dir] = world->GetSeaFromCoastalPoint(world->GetNeighbour(pos, dir));

    // Post versenden
    SendPostMessage(player,
                    std::make_unique<PostMsgWithBuilding>(GetEvMgr().GetCurrentGF(), _("New harbor building finished"),
                                                          PostCategory::Economy, *this));
}

void nobHarborBuilding::DestroyBuilding()
{
    GetEvMgr().RemoveEvent(orderware_ev);

    // Der Wirtschaftsverwaltung Bescheid sagen
    GamePlayer& owner = world->GetPlayer(player);

    // Baumaterialien in der Inventur verbuchen
    if(expedition.active)
    {
        owner.DecreaseInventoryWare(GoodType::Boards, expedition.boards);
        owner.DecreaseInventoryWare(GoodType::Stones, expedition.stones);

        // Und Bauarbeiter (später) rausschicken
        if(expedition.builder)
            inventory.Add(Job::Builder);
        else
            owner.OneJobNotWanted(Job::Builder, this);
    }
    // cancel order for scouts
    if(exploration_expedition.active)
    {
        inventory.real.Add(Job::Scout, exploration_expedition.scouts);
        for(unsigned i = exploration_expedition.scouts; i < world->GetGGS().GetNumScoutsExpedition(); i++)
            owner.OneJobNotWanted(Job::Scout, this);
    }
    // cancel all jobs wanted for this building
    owner.JobNotWanted(this, true);
    // Waiting Wares löschen
    for(auto& wares_for_ship : wares_for_ships)
    {
        wares_for_ship->WareLost(player);
        wares_for_ship->Destroy();
    }
    wares_for_ships.clear();

    // Leute, die noch aufs Schiff warten, rausschicken
    for(auto& figures_for_ship : figures_for_ships)
    {
        noFigure& figure = world->AddFigure(pos, std::move(figures_for_ship.fig));

        figure.Abrogate();
        figure.StartWandering();
        figure.StartWalking(RANDOM_ENUM(Direction));
    }
    figures_for_ships.clear();

    for(auto& soldiers_for_ship : soldiers_for_ships)
    {
        nofAttacker& soldier = world->AddFigure(pos, std::move(soldiers_for_ship.attacker));

        soldier.CancelSeaAttack();
        RTTR_Assert(!soldier.GetAttackedGoal());
        RTTR_Assert(soldier.HasNoHome());
        RTTR_Assert(soldier.HasNoGoal());
        soldier.StartWandering();
        soldier.StartWalking(RANDOM_ENUM(Direction));
    }
    soldiers_for_ships.clear();

    nobBaseWarehouse::DestroyBuilding();

    world->GetMilitarySquares().Remove(this);
    // Recalc territory. AFTER calling base destroy as otherwise figures might get stuck here
    world->RecalcTerritory(*this, TerritoryChangeReason::Destroyed);
}

void nobHarborBuilding::Serialize(SerializedGameData& sgd) const
{
    nobBaseWarehouse::Serialize(sgd);
    expedition.Serialize(sgd);
    exploration_expedition.Serialize(sgd);
    sgd.PushEvent(orderware_ev);
    helpers::pushContainer(sgd, seaIds);
    sgd.PushObjectContainer(wares_for_ships, true);
    sgd.PushUnsignedInt(figures_for_ships.size());
    for(const auto& figures_for_ship : figures_for_ships)
    {
        helpers::pushPoint(sgd, figures_for_ship.dest);
        sgd.PushObject(figures_for_ship.fig);
    }
    sgd.PushUnsignedInt(soldiers_for_ships.size());
    for(const auto& soldiers_for_ship : soldiers_for_ships)
    {
        helpers::pushPoint(sgd, soldiers_for_ship.dest);
        sgd.PushObject(soldiers_for_ship.attacker, true);
    }
}

nobHarborBuilding::nobHarborBuilding(SerializedGameData& sgd, const unsigned obj_id)
    : nobBaseWarehouse(sgd, obj_id), expedition(sgd), exploration_expedition(sgd), orderware_ev(sgd.PopEvent())
{
    // ins Militärquadrat einfügen
    world->GetMilitarySquares().Add(this);

    helpers::popContainer(sgd, seaIds);

    sgd.PopObjectContainer(wares_for_ships, GO_Type::Ware);

    unsigned count = sgd.PopUnsignedInt();
    for(unsigned i = 0; i < count; ++i)
    {
        const MapPoint dest = sgd.PopMapPoint();
        auto* fig = sgd.PopObject<noFigure>();
        figures_for_ships.emplace_back(FigureForShip{std::unique_ptr<noFigure>(fig), dest});
    }

    count = sgd.PopUnsignedInt();
    for(unsigned i = 0; i < count; ++i)
    {
        const MapPoint dest = sgd.PopMapPoint();
        auto* fig = sgd.PopObject<nofAttacker>(GO_Type::NofAttacker);
        soldiers_for_ships.emplace_back(SoldierForShip{std::unique_ptr<nofAttacker>(fig), dest});
    }
}

// Relative Position des Bauarbeiters
constexpr helpers::EnumArray<Position, Nation> BUILDER_POS = {
  {Position(-20, 18), Position(-28, 17), Position(-20, 15), Position(-38, 17), Position(-38, 17)}};
/// Relative Position der Brettertürme
constexpr helpers::EnumArray<Position, Nation> BOARDS_POS = {
  {Position(-75, -5), Position(-60, -5), Position(-55, -5), Position(-65, -5), Position(-65, -5)}};
/// Relative Position der Steintürme
constexpr helpers::EnumArray<Position, Nation> STONES_POS = {
  {Position(-65, 10), Position(-52, 10), Position(-42, 10), Position(-52, 10), Position(-52, 10)}};
/// Relative Postion der inneren Hafenfeuer
constexpr helpers::EnumArray<Position, Nation> FIRE_POS = {
  {Position(36, -51), Position(0, 0), Position(0, 0), Position(5, -80), Position(0, 0)}};
/// Relative Postion der äußeren Hafenfeuer
constexpr helpers::EnumArray<Position, Nation> EXTRAFIRE_POS = {
  {Position(0, 0), Position(0, 0), Position(8, -115), Position(0, 0), Position(0, 0)}};

void nobHarborBuilding::Draw(DrawPoint drawPt)
{
    // Gebäude an sich zeichnen
    DrawBaseBuilding(drawPt);

    // Hafenfeuer zeichnen // TODO auch für nicht-römer machen
    if(nation == Nation::Romans || nation == Nation::Japanese || nation == Nation::Babylonians)
    {
        LOADER.GetNationImage(nation, 500 + 5 * GAMECLIENT.GetGlobalAnimation(8, 2, 1, GetObjId() + GetX() + GetY()))
          ->DrawFull(drawPt + FIRE_POS[nation]);
    } else if(nation == Nation::Africans || nation == Nation::Vikings)
    {
        LOADER.GetMapTexture(740 + GAMECLIENT.GetGlobalAnimation(8, 5, 2, GetObjId() + GetX() + GetY()))
          ->DrawFull(drawPt + FIRE_POS[nation]);
    }

    if(nation == Nation::Romans)
    {
        // Zusätzliches Feuer
        LOADER.GetMapTexture(740 + GAMECLIENT.GetGlobalAnimation(8, 5, 2, GetObjId() + GetX() + GetY()))
          ->DrawFull(drawPt + EXTRAFIRE_POS[nation]);
    }

    // Läuft gerade eine Expedition?
    if(expedition.active)
    {
        // Waren für die Expedition zeichnen

        // Bretter
        DrawPoint boardsPos = drawPt + BOARDS_POS[nation];
        for(unsigned char i = 0; i < expedition.boards; ++i)
            LOADER.GetWareStackTex(GoodType::Boards)->DrawFull(boardsPos - DrawPoint(0, i * 4));
        DrawPoint stonesPos = drawPt + STONES_POS[nation];
        // Steine
        for(unsigned char i = 0; i < expedition.stones; ++i)
            LOADER.GetWareStackTex(GoodType::Stones)->DrawFull(stonesPos - DrawPoint(0, i * 4));

        // Und den Bauarbeiter, falls er schon da ist
        if(expedition.builder)
        {
            unsigned id = GAMECLIENT.GetGlobalAnimation(1000, 7, 1, GetX() + GetY());

            const int WALKING_DISTANCE = 30;

            // Wegstrecke, die er von einem Punkt vom anderen schon gelaufen ist
            int walking_distance = (id % 500) * WALKING_DISTANCE / 500;
            // Id vom laufen
            unsigned walking_id = (id / 32) % 8;

            DrawPoint builderPos = drawPt + BUILDER_POS[nation];
            if(id < 500)
                LOADER.getBobSprite(nation, Job::Builder, Direction::West, walking_id)
                  .draw(builderPos - DrawPoint(walking_distance, 0), COLOR_WHITE, world->GetPlayer(player).color);
            else
                LOADER.getBobSprite(nation, Job::Builder, Direction::East, walking_id)
                  .draw(builderPos + DrawPoint(walking_distance - WALKING_DISTANCE, 0), COLOR_WHITE,
                        world->GetPlayer(player).color);
        }
    }
}

void nobHarborBuilding::HandleEvent(const unsigned id)
{
    switch(id)
    {
        case 10:
            // Waren-Bestell-Event
            this->orderware_ev = nullptr;
            // Mal wieder schauen, ob es Waren für unsere Expedition gibt
            OrderExpeditionWares();
            break;
        default: HandleBaseEvent(id); break;
    }
}

/// Startet eine Expedition
void nobHarborBuilding::StartExpedition()
{
    // Schon eine Expedition gestartet?
    if(expedition.active)
        return;

    // Initialisierung
    expedition.active = true;

    // In unseren Warenbestand gucken und die erforderlichen Bretter und Steine sowie den
    // Bauarbeiter holen, falls vorhanden
    expedition.boards =
      std::min(unsigned(BUILDING_COSTS[BuildingType::HarborBuilding].boards), inventory[GoodType::Boards]);
    expedition.stones =
      std::min(unsigned(BUILDING_COSTS[BuildingType::HarborBuilding].stones), inventory[GoodType::Stones]);
    inventory.Remove(GoodType::Boards, expedition.boards);
    inventory.Remove(GoodType::Stones, expedition.stones);

    if(inventory[Job::Builder])
    {
        expedition.builder = true;
        inventory.Remove(Job::Builder);
    } else
    {
        bool convert = true;
        expedition.builder = false;
        // got a builder in ANY storehouse?
        GamePlayer& owner = world->GetPlayer(player);
        for(const nobBaseWarehouse* wh : owner.GetBuildingRegister().GetStorehouses()) //-V807
        {
            if(wh->GetNumRealFigures(Job::Builder))
            {
                convert = false;
                break;
            }
        }
        if(convert && inventory[GoodType::Hammer]
           && inventory[Job::Helper] > 1) // maybe have a hammer & helper to create our own builder?
        {
            inventory.Remove(GoodType::Hammer);
            owner.DecreaseInventoryWare(GoodType::Hammer, 1);
            inventory.Remove(Job::Helper);
            owner.DecreaseInventoryJob(Job::Helper, 1);

            owner.IncreaseInventoryJob(Job::Builder, 1);
            expedition.builder = true;
        }
        // not in harbor, and didnt have to or couldnt convert so order a builder
        if(!expedition.builder)
            owner.AddJobWanted(Job::Builder, this);
    }

    // Ggf. Waren bestellen, die noch fehlen
    OrderExpeditionWares();

    // Ggf. ist jetzt alles benötigte schon da
    // Dann Schiff rufen
    CheckExpeditionReady();
}

void nobHarborBuilding::StopExpedition()
{
    if(!expedition.active)
        return;

    // Dann diese stoppen
    expedition.active = false;

    // Waren zurücktransferieren
    inventory.Add(GoodType::Boards, expedition.boards);
    inventory.Add(GoodType::Stones, expedition.stones);

    if(expedition.builder)
    {
        inventory.Add(Job::Builder);
        // Evtl. Abnehmer für die Figur wieder finden
        world->GetPlayer(player).FindWarehouseForAllJobs(Job::Builder);
    } else // todo falls noch nicht da - unterscheiden ob unterwegs oder nur bestellt - falls bestellt stornieren sonst
           // informieren damit kein ersatz geschickt wird falls was nicht klappt aufm weg
    {
        world->GetPlayer(player).OneJobNotWanted(Job::Builder, this);
    }
}

/// Startet eine Erkundungs-Expedition oder stoppt sie, wenn bereits eine stattfindet
void nobHarborBuilding::StartExplorationExpedition()
{
    // Schon eine Expedition gestartet?
    if(exploration_expedition.active)
        return;

    // Initialisierung
    exploration_expedition.active = true;
    exploration_expedition.scouts = 0;

    // Look for missing scouts
    const unsigned numScoutsRequired = world->GetGGS().GetNumScoutsExpedition();
    if(inventory[Job::Scout] < numScoutsRequired)
    {
        unsigned missing = numScoutsRequired - inventory[Job::Scout];
        // got scouts in ANY storehouse?
        GamePlayer& owner = world->GetPlayer(player);
        for(const nobBaseWarehouse* wh : owner.GetBuildingRegister().GetStorehouses()) //-V807
        {
            const unsigned numScouts = wh->GetNumRealFigures(Job::Scout);
            if(numScouts >= missing)
            {
                missing = 0;
                break;
            } else if(numScouts > 0)
                missing -= numScouts;
        }
        // Recruit missing ones if possible
        while(missing > 0 && TryRecruitJob(Job::Scout))
            missing--;
        // Order scouts, we still requires
        for(unsigned i = inventory[Job::Scout]; i < numScoutsRequired; ++i)
            owner.AddJobWanted(Job::Scout, this);
    }
    if(inventory[Job::Scout])
    {
        exploration_expedition.scouts = std::min(inventory[Job::Scout], numScoutsRequired);
        inventory.real.Remove(Job::Scout, exploration_expedition.scouts);
    }

    CheckExplorationExpeditionReady();
}

void nobHarborBuilding::StopExplorationExpedition()
{
    if(!exploration_expedition.active)
        return;
    // Dann diese stoppen
    exploration_expedition.active = false;
    // cancel order for scouts
    for(unsigned i = exploration_expedition.scouts; i < world->GetGGS().GetNumScoutsExpedition(); i++)
    {
        world->GetPlayer(player).OneJobNotWanted(Job::Scout, this);
    }
    // Erkunder zurücktransferieren
    if(exploration_expedition.scouts)
    {
        inventory.real.Add(Job::Scout, exploration_expedition.scouts);
        exploration_expedition.scouts = 0;
        // Evtl. Abnehmer für die Figur wieder finden
        world->GetPlayer(player).FindWarehouseForAllJobs(Job::Scout);
    }
}

/// Bestellt die zusätzlichen erforderlichen Waren für eine Expedition
void nobHarborBuilding::OrderExpeditionWares()
{
    RTTR_Assert(!IsBeingDestroyedNow()); // Wares should already be canceled!
    if(this->IsBeingDestroyedNow())      // don't order new stuff if we are about to be destroyed
        return;

    if(!expedition.active) // expedition no longer active?
        return;
    // Waren in der Bestellungsliste mit beachten
    unsigned boards = 0, stones = 0;
    for(const auto* ware : dependent_wares)
    {
        RTTR_Assert(ware);
        if(ware->type == GoodType::Boards)
            ++boards;
        else if(ware->type == GoodType::Stones)
            ++stones;
    }

    // Prüfen, ob jeweils noch weitere Waren bestellt werden müssen
    unsigned todo_boards = 0;
    if(boards + expedition.boards < BUILDING_COSTS[BuildingType::HarborBuilding].boards)
    {
        todo_boards = BUILDING_COSTS[BuildingType::HarborBuilding].boards - (boards + expedition.boards);
        Ware* ware;
        do
        {
            ware = world->GetPlayer(player).OrderWare(GoodType::Boards, this);
            if(ware)
            {
                RTTR_Assert(IsWareDependent(*ware));
                --todo_boards;
            }
        } while(ware && todo_boards);
    }

    unsigned todo_stones = 0;
    if(stones + expedition.stones < BUILDING_COSTS[BuildingType::HarborBuilding].stones)
    {
        todo_stones = BUILDING_COSTS[BuildingType::HarborBuilding].stones - (stones + expedition.stones);
        Ware* ware;
        do
        {
            ware = world->GetPlayer(player).OrderWare(GoodType::Stones, this);
            if(ware)
            {
                RTTR_Assert(IsWareDependent(*ware));
                --todo_stones;
            }
        } while(ware && todo_stones);
    }

    // Wenn immer noch nicht alles da ist, später noch einmal bestellen
    if(!orderware_ev)
        orderware_ev = GetEvMgr().AddEvent(this, 210, 10);
}

/// Eine bestellte Ware konnte doch nicht kommen
void nobHarborBuilding::WareLost(Ware& ware)
{
    RTTR_Assert(!IsBeingDestroyedNow());
    // ggf. neue Waren für Expedition bestellen
    if(expedition.active && (ware.type == GoodType::Boards || ware.type == GoodType::Stones))
        OrderExpeditionWares();
    nobBaseWarehouse::WareLost(ware);
}

/// Schiff ist angekommen
void nobHarborBuilding::ShipArrived(noShip& ship)
{
    // get a new job - priority is given according to this list: attack,expedition,exploration,transport
    // any attackers ready?
    if(!soldiers_for_ships.empty())
    {
        // load all soldiers that share the same target as the first soldier in the list
        std::vector<std::unique_ptr<nofAttacker>> attackers;
        MapPoint ship_dest = soldiers_for_ships.begin()->dest;

        for(auto it = soldiers_for_ships.begin(); it != soldiers_for_ships.end();)
        {
            if(it->dest == ship_dest)
            {
                inventory.visual.Remove(it->attacker->GetJobType());
                attackers.push_back(std::move(it->attacker));
                it = soldiers_for_ships.erase(it);
            } else
                ++it;
        }

        ship.PrepareSeaAttack(GetHarborPosID(), ship_dest, std::move(attackers));
        return;
    }
    // Expedition ready?
    if(expedition.active && expedition.builder
       && expedition.boards == BUILDING_COSTS[BuildingType::HarborBuilding].boards
       && expedition.stones == BUILDING_COSTS[BuildingType::HarborBuilding].stones)
    {
        // Aufräumen am Hafen
        expedition.active = false;
        // Expedition starten
        ship.StartExpedition(GetHarborPosID());
        return;
    }
    // Exploration-Expedition ready?
    if(IsExplorationExpeditionReady())
    {
        // Aufräumen am Hafen
        exploration_expedition.active = false;
        // Expedition starten
        ship.StartExplorationExpedition(GetHarborPosID());
        inventory.visual.Remove(Job::Scout, exploration_expedition.scouts);
        return;
    }

    // Gibt es Waren oder Figuren, die ein Schiff von hier aus nutzen wollen?
    if(!wares_for_ships.empty() || !figures_for_ships.empty())
    {
        // Das Ziel wird nach der ersten Figur bzw. ersten Ware gewählt
        // actually since the wares might not yet have informed the harbor that their target harbor was destroyed we
        // pick the first figure/ware with a valid target instead
        MapPoint dest;
        bool gotdest = false;
        for(const auto& figureForShip : figures_for_ships)
        {
            noBase* nb = world->GetNO(figureForShip.dest);
            if(nb->GetGOT() == GO_Type::NobHarborbuilding
               && world->GetNode(figureForShip.dest).owner
                    == player + 1) // target is a harbor and owned by the same player
            {
                dest = figureForShip.dest;
                gotdest = true;
                break;
            }
        }
        for(const auto& wareForShip : wares_for_ships)
        {
            noBase* nb = world->GetNO(wareForShip->GetNextHarbor());
            if(nb->GetGOT() == GO_Type::NobHarborbuilding
               && world->GetNode(wareForShip->GetNextHarbor()).owner == player + 1)
            {
                dest = wareForShip->GetNextHarbor();
                gotdest = true;
                break;
            }
        }
        if(gotdest)
        {
            std::list<std::unique_ptr<noFigure>> figures;

            // Figuren auswählen, die zu diesem Ziel wollen
            for(auto it = figures_for_ships.begin(); it != figures_for_ships.end() && figures.size() < SHIP_CAPACITY;)
            {
                if(it->dest == dest)
                {
                    it->fig->StartShipJourney();
                    if(it->fig->GetJobType() != Job::BoatCarrier)
                        inventory.visual.Remove(it->fig->GetJobType());
                    else
                    {
                        inventory.visual.Remove(Job::Helper);
                        inventory.visual.Remove(GoodType::Boat);
                    }
                    figures.push_back(std::move(it->fig));
                    it = figures_for_ships.erase(it);
                } else
                    ++it;
            }

            // Und noch die Waren auswählen
            std::list<std::unique_ptr<Ware>> wares;
            for(auto it = wares_for_ships.begin();
                it != wares_for_ships.end() && figures.size() + wares.size() < SHIP_CAPACITY;)
            {
                if((*it)->GetNextHarbor() == dest)
                {
                    (*it)->StartShipJourney();
                    inventory.visual.Remove(ConvertShields((*it)->type));
                    wares.push_back(std::move(*it));
                    it = wares_for_ships.erase(it);
                } else
                    ++it;
            }

            // Und das Schiff starten lassen
            ship.PrepareTransport(GetHarborPosID(), dest, std::move(figures), std::move(wares));
        }
    }
}

/// Legt eine Ware im Lagerhaus ab
void nobHarborBuilding::AddWare(std::unique_ptr<Ware> ware)
{
    if(ware->GetGoal() && ware->GetGoal() != this)
    {
        // This is not the goal but we have one -> Get new route
        ware->RecalcRoute();

        // Will diese Ware mit dem Schiff irgendwo hin fahren?
        if(ware->GetNextDir() == RoadPathDirection::Ship)
        {
            // Dann fügen wir die mal bei uns hinzu
            AddWareForShip(std::move(ware));
            return;
        } else if(ware->GetNextDir() != RoadPathDirection::None)
        {
            // Travel on roads -> Carry out
            RTTR_Assert(ware->GetGoal() != this);
            AddWaitingWare(std::move(ware));
            return;
        } else
        {
            // Pathfinding failed -> Ware would want to go here
            RTTR_Assert(ware->GetGoal() == this);
            // Regular handling below
        }
    }

    // Brauchen wir die Ware?
    if(expedition.active)
    {
        if((ware->type == GoodType::Boards && expedition.boards < BUILDING_COSTS[BuildingType::HarborBuilding].boards)
           || (ware->type == GoodType::Stones
               && expedition.stones < BUILDING_COSTS[BuildingType::HarborBuilding].stones))
        {
            if(ware->type == GoodType::Boards)
                ++expedition.boards;
            else
                ++expedition.stones;

            // Ware nicht mehr abhängig
            if(ware->GetGoal())
                RemoveDependentWare(*ware);
            // Dann zweigen wir die einfach mal für die Expedition ab
            world->GetPlayer(player).RemoveWare(*ware);

            // Ggf. ist jetzt alles benötigte da
            CheckExpeditionReady();
            return;
        }
    }

    nobBaseWarehouse::AddWare(std::move(ware));
}

/// Eine Figur geht ins Lagerhaus
void nobHarborBuilding::AddFigure(std::unique_ptr<noFigure> figure, const bool increase_visual_counts)
{
    // Brauchen wir einen Bauarbeiter für die Expedition?
    if(figure->GetJobType() == Job::Builder && expedition.active && !expedition.builder)
    {
        // Make sure the figure came from outside and was not already here waiting for a ship
        if(IsDependentFigure(*figure))
            nobBaseWarehouse::RemoveDependentFigure(*figure);
        GetEvMgr().AddToKillList(std::move(figure));

        expedition.builder = true;
        // Ggf. ist jetzt alles benötigte da
        CheckExpeditionReady();
    }
    // Brauchen wir einen Spähter für die Expedition?
    else if(figure->GetJobType() == Job::Scout && exploration_expedition.active && !IsExplorationExpeditionReady())
    {
        // Make sure the figure came from outside and was not already here waiting for a ship
        if(IsDependentFigure(*figure))
            nobBaseWarehouse::RemoveDependentFigure(*figure);
        GetEvMgr().AddToKillList(std::move(figure));

        ++exploration_expedition.scouts;
        inventory.visual.Add(Job::Scout);
        // Ggf. ist jetzt alles benötigte da
        CheckExplorationExpeditionReady();
    } else
        // ansonsten weiterdelegieren
        nobBaseWarehouse::AddFigure(std::move(figure), increase_visual_counts);
}

/// Gibt zurück, ob Expedition vollständig ist
bool nobHarborBuilding::IsExpeditionReady() const
{
    if(!expedition.active)
        return false;
    // Alles da?
    if(expedition.boards < BUILDING_COSTS[BuildingType::HarborBuilding].boards)
        return false;
    if(expedition.stones < BUILDING_COSTS[BuildingType::HarborBuilding].stones)
        return false;
    if(!expedition.builder)
        return false;

    return true;
}

/// Gibt zurück, ob Expedition vollständig ist
bool nobHarborBuilding::IsExplorationExpeditionReady() const
{
    if(!exploration_expedition.active)
        return false;
    // Alles da?
    if(exploration_expedition.scouts < world->GetGGS().GetNumScoutsExpedition())
        return false;

    return true;
}

/// Prüft, ob eine Expedition von den Waren her vollständig ist und ruft ggf. das Schiff
void nobHarborBuilding::CheckExpeditionReady()
{
    // Alles da?
    // Dann bestellen wir mal das Schiff
    if(IsExpeditionReady())
        OrderShip();
}

/// Prüft, ob eine Expedition von den Spähern her vollständig ist und ruft ggf. das Schiff
void nobHarborBuilding::CheckExplorationExpeditionReady()
{
    // Alles da?
    // Dann bestellen wir mal das Schiff
    if(IsExplorationExpeditionReady())
        OrderShip();
}

/// Schiff konnte nicht mehr kommen
void nobHarborBuilding::ShipLost(noShip* /*ship*/)
{
    // Neues Schiff bestellen
    OrderShip();
}

/// Gibt die Hafenplatz-ID zurück, auf der der Hafen steht
unsigned nobHarborBuilding::GetHarborPosID() const
{
    return world->GetHarborPointID(pos);
}

/// Abfangen, wenn ein Mann nicht mehr kommen kann --> könnte ein Bauarbeiter sein und
/// wenn wir einen benötigen, müssen wir einen neuen bestellen
void nobHarborBuilding::RemoveDependentFigure(noFigure& figure)
{
    nobBaseWarehouse::RemoveDependentFigure(figure);
    // Ist das ein Bauarbeiter und brauchen wir noch einen
    if(figure.GetJobType() == Job::Builder && expedition.active && !expedition.builder)
    {
        // Alle Figuren durchkommen, die noch hierher kommen wollen und gucken, ob ein
        // Bauarbeiter dabei ist
        for(noFigure* dependent_figure : dependent_figures)
        {
            if(dependent_figure->GetJobType() == Job::Builder)
                // Brauchen keinen bestellen, also raus
                return;
        }

        // Keinen gefunden, also müssen wir noch einen bestellen
        world->GetPlayer(player).AddJobWanted(Job::Builder, this);
    }

    // Ist das ein Erkunder und brauchen wir noch welche?
    else if(figure.GetJobType() == Job::Scout && exploration_expedition.active)
    {
        unsigned scouts_coming = 0;
        for(noFigure* dependent_figure : dependent_figures)
        {
            if(dependent_figure->GetJobType() == Job::Scout)
                ++scouts_coming;
        }

        // Wenn nicht genug Erkunder mehr kommen, müssen wir einen neuen bestellen
        if(exploration_expedition.scouts + scouts_coming < world->GetGGS().GetNumScoutsExpedition())
            world->GetPlayer(player).AddJobWanted(Job::Scout, this);
    }
}

/// Gibt eine Liste mit möglichen Verbindungen zurück
std::vector<nobHarborBuilding::ShipConnection> nobHarborBuilding::GetShipConnections() const
{
    std::vector<ShipConnection> connections;

    // Is the harbor being destroyed right now? Could happen due to pathfinding for wares that get notified about this
    // buildings destruction
    if(IsBeingDestroyedNow())
        return connections;

    // Should already be handled by the above check, but keep the runtime check for now (TODO: remove runtime check)
    RTTR_Assert(world->GetGOT(pos) == GO_Type::NobHarborbuilding);

    // Is there any harbor building at all? (could be destroyed)?
    if(world->GetGOT(pos) != GO_Type::NobHarborbuilding)
        return connections;

    std::vector<nobHarborBuilding*> harbor_buildings;
    for(unsigned short seaId : seaIds)
    {
        if(seaId != 0)
            world->GetPlayer(player).GetHarborsAtSea(harbor_buildings, seaId);
    }

    for(auto* harbor_building : harbor_buildings)
    {
        ShipConnection sc;
        sc.dest = harbor_building;
        // Use twice the distance as cost (ship might need to arrive first) and a fixed value to represent
        // loading&unloading
        sc.way_costs = 2 * world->CalcHarborDistance(GetHarborPosID(), harbor_building->GetHarborPosID()) + 10;
        connections.push_back(sc);
    }
    return connections;
}

/// Fügt einen Mensch hinzu, der mit dem Schiff irgendwo hin fahren will
void nobHarborBuilding::AddFigureForShip(std::unique_ptr<noFigure> fig, MapPoint dest)
{
    RTTR_Assert(!world->HasFigureAt(fig->GetPos(), *fig)); // Figure is in the harbor, so it cannot be outside
    // Anzahl visuell erhöhen
    if(fig->GetJobType() != Job::BoatCarrier)
        inventory.visual.Add(fig->GetJobType());
    else
    {
        inventory.visual.Add(Job::Helper);
        inventory.visual.Add(GoodType::Boat);
    }
    figures_for_ships.emplace_back(FigureForShip{std::move(fig), dest});
    OrderShip();
}

/// Fügt eine Ware hinzu, die mit dem Schiff verschickt werden soll
void nobHarborBuilding::AddWareForShip(std::unique_ptr<Ware> ware)
{
    // Anzahl visuell erhöhen
    inventory.visual.Add(ConvertShields(ware->type));
    ware->WaitForShip(this);
    wares_for_ships.emplace_back(std::move(ware));
    OrderShip();
    // Take ownership
    ware = nullptr;
}

/// Gibt Anzahl der Schiffe zurück, die noch für ausstehende Aufgaben benötigt werden
unsigned nobHarborBuilding::GetNumNeededShips() const
{
    unsigned count = 0;

    // Expedition -> 1 Schiff
    if(IsExpeditionReady())
        ++count;
    // Erkundungs-Expedition -> noch ein Schiff
    if(IsExplorationExpeditionReady())
        ++count;
    // Evtl. Waren und Figuren -> noch ein Schiff pro Ziel
    if(!figures_for_ships.empty() || !wares_for_ships.empty())
    {
        // Die verschiedenen Zielhäfen -> Für jeden Hafen ein Schiff ordern
        std::vector<MapPoint> destinations;

        for(const auto& figures_for_ship : figures_for_ships)
        {
            if(!helpers::contains(destinations, figures_for_ship.dest))
            {
                destinations.push_back(figures_for_ship.dest);
                ++count;
            }
        }

        for(const auto& wares_for_ship : wares_for_ships)
        {
            if(!helpers::contains(destinations, wares_for_ship->GetNextHarbor()))
            {
                destinations.push_back(wares_for_ship->GetNextHarbor());
                ++count;
            }
        }
    }
    // Evtl. Angreifer, die noch verschifft werden müssen
    if(!soldiers_for_ships.empty())
    {
        // Die verschiedenen Zielhäfen -> Für jeden Hafen ein Schiff ordern
        std::vector<MapPoint> different_dests;
        for(const auto& soldiers_for_ship : soldiers_for_ships)
        {
            if(!helpers::contains(different_dests, soldiers_for_ship.dest))
            {
                different_dests.push_back(soldiers_for_ship.dest);
                ++count;
            }
        }
    }

    return count;
}

/// Gibt die Wichtigkeit an, dass ein Schiff kommen muss (0 -> keine Bedürftigkeit)
int nobHarborBuilding::GetNeedForShip(unsigned ships_coming) const
{
    int points = 0;

    // Expedition -> 1 Schiff
    if(IsExpeditionReady())
    {
        if(ships_coming == 0)
            points += 100;
        else
            --ships_coming;
    }
    if(IsExplorationExpeditionReady())
    {
        if(ships_coming == 0)
            points += 100;
        else
            --ships_coming;
    }
    if(!figures_for_ships.empty() || !wares_for_ships.empty())
    {
        if(ships_coming)
            --ships_coming;
        else
            points += (figures_for_ships.size() + wares_for_ships.size()) * 5;
    }

    if(!soldiers_for_ships.empty() && ships_coming == 0)
        points += (soldiers_for_ships.size() * 10);

    return points;
}

// try to order any ship that might be needed and is not ordered yet
void nobHarborBuilding::OrderShip()
{
    unsigned needed = GetNumNeededShips();
    GamePlayer& owner = world->GetPlayer(player);

    // Order (possibly) remaining ships
    for(unsigned ordered = owner.GetShipsToHarbor(*this); ordered < needed; ++ordered)
        owner.OrderShip(*this);
}

/// Abgeleitete kann eine gerade erzeugte Ware ggf. sofort verwenden
/// (muss in dem Fall true zurückgeben)
bool nobHarborBuilding::UseWareAtOnce(std::unique_ptr<Ware>& ware, noBaseBuilding& goal)
{
    // If the ware is coming to us, there is nothing to do
    if(&goal == this)
        return false;
    // Evtl. muss die Ware gleich das Schiff nehmen -> dann zum Schiffsreservoir hinzufügen
    // Assert: This is a ware that got ordered. There MUST be a path to the goal
    //         Otherwise the ware will notify the goal which will order a new ware resulting in an infinite loop
    RTTR_Assert(world->GetRoadPathFinder().PathExists(*this, goal, true));
    ware->RecalcRoute(); // Also sets nextHarbor!
    RTTR_Assert(ware->GetNextDir() != RoadPathDirection::None);
    if(ware->GetNextDir() == RoadPathDirection::Ship)
    {
        // Dann fügen wir die mal bei uns hinzu
        AddWareForShip(std::move(ware));
        return true;
    } else
        return false;
}

/// Dasselbe für Menschen
bool nobHarborBuilding::UseFigureAtOnce(std::unique_ptr<noFigure>& fig, noRoadNode& goal)
{
    // If the figure needs to take a ship, add it to the ships inventory

    // If the goal is this building don't handle it
    if(this == &goal)
        return false;

    MapPoint next_harbor;
    if(world->FindHumanPathOnRoads(*this, goal, nullptr, &next_harbor) == RoadPathDirection::Ship)
    {
        // Reduce figure count because figures don't go through the house leaving process
        // And therefore the visual count reducement
        if(fig->GetJobType() != Job::BoatCarrier)
            inventory.visual.Remove(fig->GetJobType());
        else
        {
            inventory.visual.Remove(Job::Helper);
            inventory.visual.Remove(GoodType::Boat);
        }
        // Dann fügen wir die mal bei uns hinzu
        AddFigureForShip(std::move(fig), next_harbor);
        return true;
    }

    return false;
}

/// Erhält die Waren von einem Schiff und nimmt diese in den Warenbestand auf
void nobHarborBuilding::ReceiveGoodsFromShip(std::list<std::unique_ptr<noFigure>>& figures,
                                             std::list<std::unique_ptr<Ware>>& wares)
{
    // Menschen zur Ausgehliste hinzufügen
    for(auto& figure : figures)
    {
        figure->ArrivedByShip(pos);

        // Wenn es kein Ziel mehr hat, sprich keinen weiteren Weg, kann es direkt hier gelagert werden
        if(figure->GetGoal() == this)
        {
            figure->SetGoalTonullptr();
            AddFigure(std::move(figure), true);
        } else if(figure->HasNoGoal())
        {
            AddDependentFigure(*figure); // No goal? We take it
            AddFigure(std::move(figure), true);
        } else
        {
            RoadPathDirection nextDir;
            MapPoint next_harbor = figure->ExamineRouteBeforeShipping(nextDir); //-V821

            if(nextDir == RoadPathDirection::SouthEast)
            {
                // Increase visual count
                if(figure->GetJobType() == Job::BoatCarrier)
                {
                    inventory.visual.Add(Job::Helper);
                    inventory.visual.Add(GoodType::Boat);
                } else
                    inventory.visual.Add(figure->GetJobType());
                AddLeavingFigure(std::move(figure));
            } else if(nextDir == RoadPathDirection::Ship)
            {
                AddFigureForShip(std::move(figure), next_harbor);
            } else
            {
                // No or invalid path -> Store here
                RTTR_Assert(nextDir == RoadPathDirection::None);
                figure->SetGoalTonullptr();
                AddDependentFigure(*figure);
                AddFigure(std::move(figure), true);
            }
        }
    }
    figures.clear();

    // Waren zur Warteliste hinzufügen
    for(auto& ware : wares)
    {
        ware->ShipJorneyEnded(this);
        AddWare(std::move(ware));
    }
    wares.clear();
}

nofAggressiveDefender* nobHarborBuilding::SendAggressiveDefender(nofAttacker& attacker)
{
    // Don't sent out last soldier
    unsigned numSoldiers = 0;
    for(const Job rankJob : SOLDIER_JOBS)
    {
        numSoldiers += inventory[rankJob];
        if(numSoldiers > 1)
            break;
    }
    if(numSoldiers <= 1)
        return nullptr;
    return nobBaseWarehouse::SendAggressiveDefender(attacker);
}

/// Storniert die Bestellung für eine bestimmte Ware, die mit einem Schiff transportiert werden soll
std::unique_ptr<Ware> nobHarborBuilding::CancelWareForShip(Ware* ware)
{
    // Ware zur Inventur hinzufügen
    inventory.real.Add(ConvertShields(ware->type));
    return helpers::extractPtr(wares_for_ships, ware);
}

/// Bestellte Figur, die sich noch inder Warteschlange befindet, kommt nicht mehr und will rausgehauen werden
void nobHarborBuilding::CancelFigure(noFigure* figure)
{
    const auto it =
      helpers::find_if(figures_for_ships, [figure](const FigureForShip& it) { return it.fig.get() == figure; });

    // Figur ggf. aus der List entfernen
    if(it != figures_for_ships.end())
    {
        // Dann zu unserem Inventar hinzufügen
        AddFigure(std::move(it->fig), false);
        figures_for_ships.erase(it);
    }
    // An Basisklasse weiterdelegieren
    else
        nobBaseWarehouse::CancelFigure(figure);
}

/// Gibt verfügbare Angreifer zurück
std::vector<nobHarborBuilding::SeaAttackerBuilding> nobHarborBuilding::GetAttackerBuildingsForSeaIdAttack()
{
    std::vector<nobHarborBuilding::SeaAttackerBuilding> buildings;
    sortedMilitaryBlds all_buildings = world->LookForMilitaryBuildings(pos, 3);
    // Und zählen
    for(nobBaseMilitary* all_building : all_buildings)
    {
        if(all_building->GetGOT() != GO_Type::NobMilitary)
            continue;

        // Liegt er auch im groben Raster und handelt es sich um den gleichen Besitzer?
        if(all_building->GetPlayer() != player
           || world->CalcDistance(all_building->GetPos(), pos) > BASE_ATTACKING_DISTANCE)
            continue;
        // Gebäude suchen, vielleicht schon vorhanden? Dann können wir uns den pathfinding Aufwand sparen!
        if(helpers::contains(buildings, static_cast<nobMilitary*>(all_building)))
        {
            // Dann zum nächsten test
            continue;
        }
        // Weg vom Hafen zum Militärgebäude berechnen
        if(!world->FindHumanPath(all_building->GetPos(), pos, MAX_ATTACKING_RUN_DISTANCE))
            continue;
        // neues Gebäude mit weg und allem -> in die Liste!
        SeaAttackerBuilding sab = {static_cast<nobMilitary*>(all_building), this, 0};
        buildings.push_back(sab);
    }
    return buildings;
}
/// Gibt die Angreifergebäude zurück, die dieser Hafen für einen Seeangriff zur Verfügung stellen kann
std::vector<nobHarborBuilding::SeaAttackerBuilding>
nobHarborBuilding::GetAttackerBuildingsForSeaAttack(const std::vector<unsigned>& defender_harbors)
{
    std::vector<nobHarborBuilding::SeaAttackerBuilding> buildings;
    sortedMilitaryBlds all_buildings = world->LookForMilitaryBuildings(pos, 3);
    // Und zählen
    for(nobBaseMilitary* all_building : all_buildings)
    {
        if(all_building->GetGOT() != GO_Type::NobMilitary)
            continue;

        // Liegt er auch im groben Raster und handelt es sich um den gleichen Besitzer?
        if(all_building->GetPlayer() != player
           || world->CalcDistance(all_building->GetPos(), pos) > BASE_ATTACKING_DISTANCE)
            continue;

        // Weg vom Hafen zum Militärgebäude berechnen
        if(!world->FindHumanPath(all_building->GetPos(), pos, MAX_ATTACKING_RUN_DISTANCE))
            continue;

        // Entfernung zwischen Hafen und möglichen Zielhafenpunkt ausrechnen
        unsigned min_distance = 0xffffffff;
        for(unsigned int defender_harbor : defender_harbors)
        {
            min_distance = std::min(min_distance, world->CalcHarborDistance(GetHarborPosID(), defender_harbor));
        }

        // Gebäude suchen, vielleicht schon vorhanden?
        auto it2 = helpers::find(buildings, static_cast<nobMilitary*>(all_building));
        // Noch nicht vorhanden?
        if(it2 == buildings.end())
        {
            // Dann neu hinzufügen
            SeaAttackerBuilding sab = {static_cast<nobMilitary*>(all_building), this, min_distance};
            buildings.push_back(sab);
        }
        // Oder vorhanden und jetzige Distanz ist kleiner?
        else if(min_distance < it2->distance)
        {
            // Dann Distanz und betreffenden Hafen aktualisieren
            it2->distance = min_distance;
            it2->harbor = this;
        }
    }
    return buildings;
}

/// Fügt einen Schiffs-Angreifer zum Hafen hinzu
void nobHarborBuilding::AddSeaAttacker(std::unique_ptr<nofAttacker> attacker)
{
    unsigned best_distance = 0xffffffff;
    unsigned best_harbor_point = 0xffffffff;
    RTTR_Assert(attacker->GetAttackedGoal());
    std::vector<unsigned> harbor_points =
      world->GetHarborPointsAroundMilitaryBuilding(attacker->GetAttackedGoal()->GetPos());
    for(unsigned int harbor_point : harbor_points)
    {
        unsigned tmp_distance = world->CalcHarborDistance(this->GetHarborPosID(), harbor_point);
        if(tmp_distance < best_distance)
        {
            best_distance = tmp_distance;
            best_harbor_point = harbor_point;
        }
    }

    // no harbor to target (should not happen) or no target (might happen very very rarely not sure)
    if(best_harbor_point == 0xffffffff)
    {
        // notify target about noShow, notify home that soldier wont return, add to inventory
        attacker->SeaAttackFailedBeforeLaunch(); // set state, remove target & home
        RTTR_Assert(!attacker->GetAttackedGoal());
        RTTR_Assert(attacker->HasNoHome());
        RTTR_Assert(attacker->HasNoGoal());
        AddFigure(std::move(attacker), true);
        return;
    }

    inventory.visual.Add(attacker->GetJobType());
    soldiers_for_ships.emplace_back(SoldierForShip{std::move(attacker), world->GetHarborPoint(best_harbor_point)});

    OrderShip();
}

void nobHarborBuilding::CancelSeaAttacker(nofAttacker* attacker)
{
    const auto it =
      helpers::find_if(soldiers_for_ships, [attacker](const auto& it) { return it.attacker.get() == attacker; });

    RTTR_Assert(it != soldiers_for_ships.end());
    if(attacker->HasNoGoal())
    {
        // No goal? We take it
        AddDependentFigure(*attacker);
        AddFigure(std::move(it->attacker), false);
    } else
        AddLeavingFigure(std::move(it->attacker)); // Just let him leave so he can go home
    soldiers_for_ships.erase(it);
}

unsigned nobHarborBuilding::CalcDistributionPoints(const GoodType type) const
{
    // Ist überhaupt eine Expedition im Gang und ein entsprechender Warentyp
    if(!expedition.active || (type != GoodType::Boards && type != GoodType::Stones))
        return 0;

    unsigned ordered_boards = 0, ordered_stones = 0;
    // Ermitteln, wiviele Bretter und Steine auf dem Weg zum Lagerhaus sind
    for(auto* dependent_ware : dependent_wares)
    {
        if(dependent_ware->type == GoodType::Boards)
            ++ordered_boards;
        else if(dependent_ware->type == GoodType::Stones)
            ++ordered_stones;
    }

    // 10000 als Basis wählen, damit man auch noch was abziehen kann
    unsigned short points = 10000;

    // Ermitteln, ob wir noch Bretter oder Steine brauchen
    if(expedition.boards + ordered_boards >= BUILDING_COSTS[BuildingType::HarborBuilding].boards
       && type == GoodType::Boards)
        return 0;
    if(expedition.stones + ordered_stones >= BUILDING_COSTS[BuildingType::HarborBuilding].stones
       && type == GoodType::Stones)
        return 0;

    // Schon bestellte Sachen wirken sich positiv aus, da wir ja so eher eine Expedition bereit haben
    if(type == GoodType::Boards)
        points += (expedition.boards + ordered_boards) * 30;
    else if(type == GoodType::Stones)
        points += (expedition.stones + ordered_stones) * 30;

    return points;
}

/// A ware changed its route and doesn't want to use the ship anymore
void nobHarborBuilding::WareDontWantToTravelByShip(Ware* ware)
{
    // Maybe this building is already destroyed
    if(world->GetGOT(pos) != GO_Type::NobHarborbuilding)
        return;

    // Move to waiting_wares
    waiting_wares.push_back(helpers::extractPtr(wares_for_ships, ware));
    ware->WaitInWarehouse(this);
    // Carry out. If it would want to go back to this building, then this will be handled by the carrier
    AddLeavingEvent();
}

/// Stellt Verteidiger zur Verfügung
std::unique_ptr<nofDefender> nobHarborBuilding::ProvideDefender(nofAttacker& attacker)
{
    // Versuchen, zunächst auf konventionelle Weise Angreifer zu bekoommen
    std::unique_ptr<nofDefender> defender = nobBaseWarehouse::ProvideDefender(attacker);
    // Wenn das nicht geklappt hat und noch Soldaten in der Warteschlange für den Seeangriff sind
    // zweigen wir einfach diese ab
    if(!defender && !soldiers_for_ships.empty())
    {
        std::unique_ptr<nofAttacker> defender_attacker = std::move(soldiers_for_ships.begin()->attacker);
        soldiers_for_ships.pop_front();
        defender = std::make_unique<nofDefender>(pos, player, *this, defender_attacker->GetRank(), attacker);
        defender_attacker->CancelSeaAttack();
        defender_attacker->Destroy();
    }

    return defender;
}

/// People waiting for a ship have to examine their route if a road was destroyed
void nobHarborBuilding::ExamineShipRouteOfPeople()
{
    for(auto it = figures_for_ships.begin(); it != figures_for_ships.end();)
    {
        auto& fig = it->fig;
        RoadPathDirection nextDir;
        it->dest = fig->ExamineRouteBeforeShipping(nextDir);

        if(nextDir == RoadPathDirection::None)
        {
            // No route found!
            // I.E. insert the worker in this harbor
            AddDependentFigure(*fig);
            AddFigure(std::move(fig), false);
            it = figures_for_ships.erase(it);
        } else if(nextDir != RoadPathDirection::Ship)
        {
            // Figure want to continue walking to its goal but not on ship anymore
            this->AddLeavingFigure(std::move(fig));
            it = figures_for_ships.erase(it);
        } else
            // Otherwise figure want to travel by ship, do nothing!
            ++it;
    }
}

bool nobHarborBuilding::IsBeingDestroyedNow() const
{
    // check if this harbor is in the known harbors. if not, it is probably being destroyed right now.
    return !helpers::contains(world->GetPlayer(player).GetBuildingRegister().GetHarbors(), this);
}
