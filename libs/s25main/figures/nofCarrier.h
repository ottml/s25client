// Copyright (C) 2005 - 2024 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "figures/noFigure.h"
#include <cstdint>
#include <memory>
#include <vector>

class RoadSegment;
class Ware;
class noRoadNode;
class SerializedGameData;

enum class CarrierState : uint8_t
{
    FigureWork,              // Aufgaben der Figur
    WaitForWare,             // auf Weg auf Ware warten
    GotoMiddleOfRoad,        // zur Mitte seines Weges gehen
    FetchWare,               // Ware holen
    CarryWare,               // Ware zur Flagge tragen
    CarryWareToBuilding,     // Ware zum Gebäude schaffen
    LeaveBuilding,           // kommt aus Gebäude wieder raus (bzw kommt von Baustelle zurück) zum Weg
    WaitForWareSpace,        // wartet vor der Flagge auf einen freien Platz
    GoBackFromFlag,          // geht von der Flagge zurück, weil kein Platz mehr frei war
    BoatcarrierWanderOnWater // Rumirren der Bootsträger auf dem Wasser, d.h. Paddeln zum
    // nächsten Ufer, nachdem der Wasserweg zerstört wurde
};
constexpr auto maxEnumValue(CarrierState)
{
    return CarrierState::BoatcarrierWanderOnWater;
}

enum class CarrierType : uint8_t
{
    Normal, // Normaler Träger
    Donkey, // Esel
    Boat    // Träger mit Boot
};
constexpr auto maxEnumValue(CarrierType)
{
    return CarrierType::Boat;
}

class nofCarrier : public noFigure
{
private:
    CarrierType ct;
    /// Was der Träger gerade so treibt
    CarrierState state;
    /// Ist er dick?
    bool fat;
    // Weg, auf dem er arbeitet
    RoadSegment* workplace;
    /// Ware, die er gerade trägt (0 = nichts)
    std::unique_ptr<Ware> carried_ware;
    /// Rechne-Produktivität-aus-Event
    const GameEvent* productivity_ev;
    // Letzte errechnete Produktivität
    unsigned productivity;
    /// Wieviel GF von einer bestimmten Anzahl in diesem Event-Zeitraum gearbeitet wurde
    unsigned worked_gf;
    /// Zu welchem GF das letzte Mal das Arbeiten angefangen wurde
    unsigned since_working_gf;
    /// Bestimmt GF der nächsten Trägeranimation
    unsigned next_animation;
    /// For boat carriers: path to the shore
    std::vector<Direction> shore_path;

    void GoalReached() override;
    void Walked() override;
    void AbrogateWorkplace() override;
    /// Make the carrier loose/destroy the ware if he has any
    void LooseWare();

    void HandleDerivedEvent(unsigned id) override;

    /// Nach dem Tragen der Ware, guckt der Träger an beiden Flagge, obs Waren gibt, holt/trägt diese ggf oder geht
    /// ansonsten wieder in die Mitte
    void LookForWares();
    /// Nimmt eine Ware auf an der aktuellen Flagge und dreht sich um, um sie zu tragen (fetch_dir ist die Richtung der
    /// Waren, die der Träger aufnehmen will)
    void FetchWare(bool swap_wares);

    /// Prüft, ob die getragene Ware dann von dem Weg zum Gebäude will
    bool WantInBuilding(bool* calculated);

    /// Für Produktivitätsmessungen: fängt an zu arbeiten
    void StartWorking();
    /// Für Produktivitätsmessungen: hört auf zu arbeiten
    void StopWorking();

    /// Bestimmt neuen Animationszeitpunkt
    void SetNewAnimationMoment();

    /// Boat carrier paddles to the coast after his road was destroyed
    void WanderOnWater();

public:
    nofCarrier(CarrierType ct, MapPoint pos, unsigned char player, RoadSegment* workplace, noRoadNode* goal);
    nofCarrier(SerializedGameData& sgd, unsigned obj_id);
    nofCarrier(const nofCarrier&) = delete;

    ~nofCarrier() override;

    void Serialize(SerializedGameData& sgd) const override;

    void Destroy() override;

    GO_Type GetGOT() const final { return GO_Type::NofCarrier; }

    /// Gibt Träger-Typ zurück
    CarrierType GetCarrierType() const { return ct; }
    /// Was macht der Träger gerade?
    CarrierState GetCarrierState() const { return state; }
    /// Gibt Träger-Produktivität in % zurück
    unsigned GetProductivity() const { return productivity; }

    void Draw(DrawPoint drawPt) override;

    /// Wird aufgerufen, wenn der Weg des Trägers abgerissen wurde
    void LostWork();

    /// Wird aufgerufen, wenn der Arbeitsplatz des Trägers durch eine Flagge geteilt wurde
    /// der Träger sucht sich damit einen der beiden als neuen Arbeitsplatz, geht zur Mitte und ruft einen neuen Träger
    /// für das 2. Wegstück
    void RoadSplitted(RoadSegment* rs1, RoadSegment* rs2);

    /// Sagt dem Träger Bescheid, dass es an einer Flagge noch eine Ware zu transportieren gibt
    bool AddWareJob(const noRoadNode* rn);
    /// Das Gegnteil von AddWareJob: wenn eine Ware nicht mehr transportiert werden will, sagt sie dem Träger Bescheid,
    /// damit er nicht unnötig dort hinläuft zur Flagge
    void RemoveWareJob();

    /// Benachrichtigt den Träger, wenn an einer auf seinem Weg an einer Flagge wieder ein freier Platz ist
    /// gibt zurück, ob der Träger diesen freien Platz auch nutzen wird
    bool SpaceAtFlag(bool flag);

    /// Gibt erste Flagge des Arbeitsweges zurück, falls solch einer existiert
    noRoadNode* GetFirstFlag() const;
    noRoadNode* GetSecondFlag() const;

    /// Wird aufgerufen, wenn die Straße unter der Figur geteilt wurde (für abgeleitete Klassen)
    void CorrectSplitData_Derived() override;
};
