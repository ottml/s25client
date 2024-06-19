// Copyright (C) 2024 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "CampaignProgress.h"
#include "Settings.h"

CampaignProgress::CampaignProgress(boost::filesystem::path const& campaignFolder)
    : campaignLua_(campaignFolder / "campaign.lua")
{
    // First mission is enabled per default
    EnableMission(0);
}

void CampaignProgress::resizeIfNeeded(CampaignMissionStatus& status, unsigned newMissionIdx)
{
    auto newSize =
      std::max(status.isFinished.size(), std::max(static_cast<size_t>(newMissionIdx + 1), status.isEnabled.size()));
    status.isEnabled.resize(newSize, false);
    status.isFinished.resize(newSize, false);
}

void CampaignProgress::EnableMission(unsigned missionIdx)
{
    auto& missionList = SETTINGS.campaigns.campaignStatus[campaignLua_.string()];
    resizeIfNeeded(missionList, missionIdx);
    missionList.isEnabled[missionIdx] = true;
}

void CampaignProgress::FinishMission(unsigned missionIdx)
{
    auto& missionList = SETTINGS.campaigns.campaignStatus[campaignLua_.string()];
    resizeIfNeeded(missionList, missionIdx);
    missionList.isFinished[missionIdx] = true;
}

bool CampaignProgress::IsMissionEnabled(unsigned missionIdx) const
{
    if(SETTINGS.campaigns.campaignStatus.count(campaignLua_.string()) == 0)
        return false;
    auto const& missionEnabledList = SETTINGS.campaigns.campaignStatus[campaignLua_.string()].isEnabled;
    return missionIdx < missionEnabledList.size() ? missionEnabledList[missionIdx] : false;
}

bool CampaignProgress::IsMissionFinished(unsigned missionIdx) const
{
    if(SETTINGS.campaigns.campaignStatus.count(campaignLua_.string()) == 0)
        return false;
    auto const& missionFinishedList = SETTINGS.campaigns.campaignStatus[campaignLua_.string()].isFinished;
    return missionIdx < missionFinishedList.size() ? missionFinishedList[missionIdx] : false;
}
