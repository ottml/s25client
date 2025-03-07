// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "EconomyModeHandler.h"
#include "buildings/nobBaseMilitary.h"
#include "enum_cast.hpp"
#include "helpers/OptionalEnum.h"
#include "lua/LuaInterfaceGame.h"
#include "notifications/NotificationManager.h"
#include "postSystem/PostManager.h"
#include "world/World.h"
#include <memory>
#include <set>
#include <vector>

class EventManager;
class FreePathFinder;
class GameInterface;
class GamePlayer;
class GlobalGameSettings;
class nobHarborBuilding;
class noBuildingSite;
class noFlag;
class nofPassiveSoldier;
class RoadPathFinder;
class SoundManager;
class TradePathCache;

constexpr Direction getOppositeDir(const RoadDir roadDir) noexcept
{
    static_assert(rttr::enum_cast(Direction::West) == rttr::enum_cast(RoadDir::East)
                    && rttr::enum_cast(Direction::NorthWest) == rttr::enum_cast(RoadDir::SouthEast)
                    && rttr::enum_cast(Direction::NorthEast) == rttr::enum_cast(RoadDir::SouthWest),
                  "Opposite directions don't match");
    return Direction(rttr::enum_cast(roadDir));
}

constexpr Direction toDirection(const RoadDir roadDir) noexcept
{
    static_assert(rttr::enum_cast(Direction::East) == rttr::enum_cast(RoadDir::East) + 3u
                    && rttr::enum_cast(Direction::SouthEast) == rttr::enum_cast(RoadDir::SouthEast) + 3u
                    && rttr::enum_cast(Direction::SouthWest) == rttr::enum_cast(RoadDir::SouthWest) + 3u,
                  "Directions don't match");
    return Direction(rttr::enum_cast(roadDir) + 3u);
}

/// Grundlegende Klasse, die die Gamewelt darstellt, enth�lt nur deren Daten
class GameWorldBase : public World
{
    std::unique_ptr<RoadPathFinder> roadPathFinder;
    std::unique_ptr<FreePathFinder> freePathFinder;
    PostManager postManager;
    mutable NotificationManager notifications;

    std::vector<GamePlayer> players;
    const GlobalGameSettings& gameSettings;
    EventManager& em;
    std::unique_ptr<SoundManager> soundManager;
    std::set<MapPoint, MapPointLess> ptsInsideComputerBarriers;
    LuaInterfaceGame* lua;

protected:
    /// Interface zum GUI
    GameInterface* gi;
    std::unique_ptr<EconomyModeHandler> econHandler;
    std::unique_ptr<TradePathCache> tradePathCache;

public:
    GameWorldBase(std::vector<GamePlayer> players, const GlobalGameSettings& gameSettings, EventManager& em);
    ~GameWorldBase() override;

    // Grundlegende Initialisierungen
    void Init(const MapExtent& mapSize, DescIdx<LandscapeDesc> lt = DescIdx<LandscapeDesc>(0)) override;
    /// Create Trade graphs
    virtual void CreateTradeGraphs() = 0;
    // Remaining initialization after loading (BQ...)
    void InitAfterLoad();

    GameInterface* GetGameInterface() { return gi; }
    const GameInterface* GetGameInterface() const { return gi; }
    void SetGameInterface(GameInterface* const gi) { this->gi = gi; }

    /// Get the economy mode handler if set.
    /// TODO: Add const correct version, but iwEconomicProgress still needs to be able to call UpdateAmounts
    EconomyModeHandler* getEconHandler() const { return econHandler.get(); }

    /// Can a node be used for a road (no flag/bld, no other road, no danger...)
    /// Should only be used for the points between the 2 flags of a road
    bool IsRoadAvailable(bool boat_road, MapPoint pt) const;
    /// Check if this road already exists completely
    bool RoadAlreadyBuilt(bool boat_road, MapPoint start, const std::vector<Direction>& route);
    bool IsOnRoad(const MapPoint& pt) const;
    /// Check if a flag is at a neighbour node
    bool IsFlagAround(const MapPoint& pt) const;

    /// Berechnet BQ bei einer gebauten Stra�e
    void RecalcBQForRoad(MapPoint pt);
    /// Pr�ft, ob sich in unmittelbarer N�he (im Radius von 4) Milit�rgeb�ude befinden
    bool IsMilitaryBuildingNearNode(MapPoint nPt, unsigned char player) const;
    /// Return true if there is a military building or building site on the node
    /// If attackBldsOnly is true, then only troop buildings are returned
    /// Otherwise it includes e.g. HQ and harbour which cannot attack itself but hold land
    bool IsMilitaryBuildingOnNode(MapPoint pt, bool attackBldsOnly) const;
    /// Erstellt eine Liste mit allen Milit�rgeb�uden in der Umgebung, radius bestimmt wie viele K�stchen nach einer
    /// Richtung im Umkreis
    sortedMilitaryBlds LookForMilitaryBuildings(MapPoint pt, unsigned short radius) const;

    /// Finds a path for figures. Returns first direction to walk in if found
    helpers::OptionalEnum<Direction> FindHumanPath(MapPoint start, MapPoint dest, unsigned max_route = 0xFFFFFFFF,
                                                   bool random_route = false, unsigned* length = nullptr,
                                                   std::vector<Direction>* route = nullptr) const;
    /// Find path for ships to a specific harbor and see. Return true on success
    bool FindShipPathToHarbor(MapPoint start, unsigned harborId, unsigned seaId, std::vector<Direction>* route,
                              unsigned* length);
    /// Find path for ships with a limited distance. Return true on success
    bool FindShipPath(MapPoint start, MapPoint dest, unsigned maxDistance, std::vector<Direction>* route,
                      unsigned* length);
    RoadPathFinder& GetRoadPathFinder() const { return *roadPathFinder; }
    FreePathFinder& GetFreePathFinder() const { return *freePathFinder; }

    /// Return flag that is on road at given point. dir will be set to the direction of the road from the returned flag
    /// prevDir (if set) will be skipped when searching for the road points
    noFlag* GetRoadFlag(MapPoint pt, Direction& dir, helpers::OptionalEnum<Direction> prevDir = boost::none);
    const noFlag* GetRoadFlag(MapPoint pt, Direction& dir,
                              helpers::OptionalEnum<Direction> prevDir = boost::none) const;

    /// Gets the (height adjusted) global coordinates of the node (e.g. for drawing)
    Position GetNodePos(MapPoint pt) const;

    /// Berechnet Bauqualitäten an Punkt x;y und den ersten Kreis darum neu
    void RecalcBQAroundPoint(MapPoint pt);
    /// Berechnet Bauqualitäten wie bei letzterer Funktion, bloß noch den 2. Kreis um x;y herum
    void RecalcBQAroundPointBig(MapPoint pt);

    /// Ermittelt Sichtbarkeit eines Punktes auch unter Einbeziehung der Verbündeten des jeweiligen Spielers
    Visibility CalcVisiblityWithAllies(MapPoint pt, unsigned char player) const;

    /// Ist es an dieser Stelle für einen Spieler möglich einen Hafen zu bauen
    bool IsHarborPointFree(unsigned harborId, unsigned char player) const;
    /// Ermittelt, ob ein Punkt Küstenpunkt ist, d.h. Zugang zu einem schiffbaren Meer, an dem auch mindestens 1
    /// Hafenplatz liegt, hat und gibt ggf. die Meeres-ID zurück, ansonsten 0
    bool IsCoastalPointToSeaWithHarbor(MapPoint pt) const;
    /// Sucht freie Hafenpunkte, also wo noch ein Hafen gebaut werden kann
    unsigned GetNextFreeHarborPoint(MapPoint pt, unsigned origin_harborId, const ShipDirection& dir,
                                    unsigned char player) const;
    /// Bestimmt für einen beliebigen Punkt auf der Karte die Entfernung zum nächsten Hafenpunkt
    unsigned CalcDistanceToNearestHarbor(MapPoint pos) const;
    /// returns true when a harborpoint is in SEAATTACK_DISTANCE for figures!
    bool IsAHarborInSeaAttackDistance(MapPoint pos) const;

    /// Return the player with the given index
    GamePlayer& GetPlayer(unsigned id);
    const GamePlayer& GetPlayer(unsigned id) const;
    unsigned GetNumPlayers() const;
    bool IsSinglePlayer() const;
    /// Return the game settings
    const GlobalGameSettings& GetGGS() const { return gameSettings; }
    EventManager& GetEvMgr() { return em; }
    const EventManager& GetEvMgr() const { return em; }
    SoundManager& GetSoundMgr() { return *soundManager; }
    PostManager& GetPostMgr() { return postManager; }
    const PostManager& GetPostMgr() const { return postManager; }
    NotificationManager& GetNotifications() const
    {
        // We want to be abled to add notifications even on a const world
        // Hence return as non-const
        return this->notifications;
    }

    struct PotentialSeaAttacker
    {
        /// Comparator that compares only the soldier pointer
        struct CmpSoldier
        {
            nofPassiveSoldier* const search;
            CmpSoldier(nofPassiveSoldier* const search) : search(search) {}
            bool operator()(const PotentialSeaAttacker& other) const { return other.soldier == search; }
        };
        /// Soldat, der als Angreifer in Frage kommt
        nofPassiveSoldier* soldier;
        /// Hafen, den der Soldat zuerst ansteuern soll
        nobHarborBuilding* harbor;
        /// Entfernung Hafen-Hafen (entscheidende)
        unsigned distance;

        PotentialSeaAttacker(nofPassiveSoldier* soldier, nobHarborBuilding* harbor, unsigned distance)
            : soldier(soldier), harbor(harbor), distance(distance)
        {}
    };

    /// Liefert Hafenpunkte im Umkreis von einem bestimmten Milit�rgeb�ude
    std::vector<unsigned> GetHarborPointsAroundMilitaryBuilding(MapPoint pt) const;
    /// Return all harbor Ids that can be used as a landing site for attacking the given point
    /// Sets all entries in @param use_seas to true, if the given sea can be used for attacking (seaId=1 -> Index 0 as
    /// seaId=0 is invalid sea)
    std::vector<unsigned> GetUsableTargetHarborsForAttack(MapPoint targetPt, std::vector<bool>& use_seas,
                                                          unsigned char player_attacker) const;
    /// Return all sea Ids from @param usableSeas that can be used for attacking the given point
    std::vector<unsigned short> GetFilteredSeaIDsForAttack(MapPoint targetPt,
                                                           const std::vector<unsigned short>& usableSeas,
                                                           unsigned char player_attacker) const;
    /// Return all soldiers (in no specific order) that can be used to attack the given point via a sea.
    /// Checks all preconditions for a sea attack (addon, attackable...)
    std::vector<PotentialSeaAttacker> GetSoldiersForSeaAttack(unsigned char player_attacker, MapPoint pt) const;
    /// Return number or strength (summed ranks) of soldiers that can attack via the given sea
    unsigned GetNumSoldiersForSeaAttackAtSea(unsigned char player_attacker, unsigned short seaid,
                                             bool returnCount = true) const;

    /// Recalculates the BQ for the given point
    void RecalcBQ(MapPoint pt);

    void SetComputerBarrier(const MapPoint& pt, unsigned radius);
    bool IsInsideComputerBarrier(const MapPoint& pt) const;

    bool HasLua() const { return lua != nullptr; }
    LuaInterfaceGame& GetLua() const { return *lua; }
    void SetLua(LuaInterfaceGame* newLua) { lua = newLua; }

protected:
    /// Called when the visibility of point changed for a player
    void VisibilityChanged(MapPoint pt, unsigned player, Visibility oldVis, Visibility newVis) override;
    /// Called, when the altitude of a point was changed
    void AltitudeChanged(MapPoint pt) override;

private:
    /// Returns the harbor ID of the next matching harbor in the given direction (0 = None)
    /// T_IsHarborOk must be a predicate taking a harbor Id and returning a bool if the harbor is valid to return
    template<typename T_IsHarborOk>
    unsigned GetHarborInDir(MapPoint pt, unsigned origin_harborId, const ShipDirection& dir,
                            T_IsHarborOk isHarborOk) const;
};
