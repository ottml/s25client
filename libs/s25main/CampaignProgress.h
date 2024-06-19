// Copyright (C) 2024 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once
#include <boost/filesystem/path.hpp>

class CampaignProgress
{
public:
    CampaignProgress(boost::filesystem::path const& campaignFolder);

public:
    void EnableMission(unsigned missionIdx);
    void FinishMission(unsigned missionIdx);

    bool IsMissionEnabled(unsigned missionIdx) const;
    bool IsMissionFinished(unsigned missionIdx) const;

    std::vector<bool> IsMissionEnabled() const;
    std::vector<bool> IsMissionFinished() const;

private:
    boost::filesystem::path campaignLua_;
};
