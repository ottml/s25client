// Copyright (C) 2024 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "CampaignProgress.h"
#include "Settings.h"
#include "lua/CampaignDataLoader.h"
#include "mygettext/mygettext.h"
#include "gameData/CampaignDescription.h"

CampaignProgress::CampaignProgress(boost::filesystem::path const& campaignFolder)
    : campaignLua_(campaignFolder / "campaign.lua")
{
    const auto settings = std::make_unique<CampaignDescription>();
    CampaignDataLoader loader(*settings, campaignFolder);
    if(!loader.Load() || settings->getNumMaps() == 0)
        throw std::runtime_error(_("Campaign info could not be loaded."));

    auto& missionList = SETTINGS.campaigns.campaignStatus[campaignLua_.string()];
    if(missionList.isEnabled.size() != settings->getNumMaps())
    {
        missionList.isEnabled.resize(settings->getNumMaps(), false);
        missionList.isFinished.resize(settings->getNumMaps(), false);
    }

    // First mission is enabled per default
    EnableMission(0);
}

void CampaignProgress::EnableMission(unsigned missionIdx)
{
    auto& missionsEnabled = SETTINGS.campaigns.campaignStatus[campaignLua_.string()].isEnabled;
    if(missionIdx < missionsEnabled.size())
        missionsEnabled[missionIdx] = true;
}

void CampaignProgress::FinishMission(unsigned missionIdx)
{
    auto& missionsFinished = SETTINGS.campaigns.campaignStatus[campaignLua_.string()].isFinished;
    if(missionIdx < missionsFinished.size())
        missionsFinished[missionIdx] = true;
}

bool CampaignProgress::IsMissionEnabled(unsigned missionIdx) const
{
    auto& missionsEnabled = SETTINGS.campaigns.campaignStatus[campaignLua_.string()].isEnabled;
    if(missionIdx < missionsEnabled.size())
        return missionsEnabled[missionIdx];
    return false;
}

bool CampaignProgress::IsMissionFinished(unsigned missionIdx) const
{
    auto& missionsFinished = SETTINGS.campaigns.campaignStatus[campaignLua_.string()].isFinished;
    if(missionIdx < missionsFinished.size())
        return missionsFinished[missionIdx];
    return false;
}

std::vector<bool> CampaignProgress::IsMissionEnabled() const
{
    return SETTINGS.campaigns.campaignStatus[campaignLua_.string()].isEnabled;
}

std::vector<bool> CampaignProgress::IsMissionFinished() const
{
    return SETTINGS.campaigns.campaignStatus[campaignLua_.string()].isFinished;
}
