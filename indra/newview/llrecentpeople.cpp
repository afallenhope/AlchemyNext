/**
 * @file llrecentpeople.cpp
 * @brief List of people with which the user has recently interacted.
 *
 * $LicenseInfo:firstyear=2009&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "llrecentpeople.h"
#include "llgroupmgr.h"

#include "llagent.h"
#include "llsdserialize.h"
#include "llviewercontrol.h"

using namespace LLOldEvents;

static const std::string sRecentCacheFilename("recent_people.xml");

LLRecentPeople::LLRecentPeople()
:   mFilename(gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, sRecentCacheFilename))
{
    load();
}

LLRecentPeople::~LLRecentPeople()
{
    save();
}

bool LLRecentPeople::add(const LLUUID& id, LLSD& userdata)
{
    if (id == gAgent.getID())
        return false;

    bool is_not_group_id = LLGroupMgr::getInstance()->getGroupData(id) == NULL;

    if (is_not_group_id)
    {
        // Really dumb hack for persistant storage saving/loading
        userdata["id"] = id;
        //[] instead of insert to replace existing id->llsd["date"] with new date value
        mPeople[id] = userdata;
        mChangedSignal();
    }

    return is_not_group_id;
}

bool LLRecentPeople::contains(const LLUUID& id) const
{
    return mPeople.find(id) != mPeople.end();
}

void LLRecentPeople::get(uuid_vec_t& result) const
{
    result.clear();
    for (const auto& pos : mPeople) {
            result.push_back(pos.first);
    }
}

const LLDate LLRecentPeople::getDate(const LLUUID& id) const
{
    recent_people_t::const_iterator it = mPeople.find(id);
    if (it!= mPeople.end()) return it->second["date"].asDate();

    static LLDate no_date = LLDate();
    return no_date;
}

const LLSD& LLRecentPeople::getData(const LLUUID& id) const
{
    recent_people_t::const_iterator it = mPeople.find(id);

    if (it != mPeople.end())
        return it->second;

    static LLSD no_data = LLSD();
    return no_data;
}

// virtual
bool LLRecentPeople::handleEvent(LLPointer<LLEvent> event, const LLSD& userdata)
{
    (void) userdata;
    add(event->getValue().asUUID());
    return true;
}

void LLRecentPeople::updateAvatarsArrivalTime(uuid_vec_t& uuids)
{
    id_to_time_map_t buf = mAvatarsArrivalTime;
    mAvatarsArrivalTime.clear();

    for (uuid_vec_t::const_iterator id_it = uuids.begin(); id_it != uuids.end(); ++id_it)
    {
        if (buf.find(*id_it) != buf.end())
        {
            mAvatarsArrivalTime[*id_it] = buf[*id_it];
        }
        else
        {
            mAvatarsArrivalTime[*id_it] = LLDate::now().secondsSinceEpoch();
        }
    }
}

F32 LLRecentPeople::getArrivalTimeByID(const LLUUID& id)
{
    id_to_time_map_t::const_iterator it = mAvatarsArrivalTime.find(id);

    if (it != mAvatarsArrivalTime.end())
    {
        return it->second;
    }
    return LLDate::now().secondsSinceEpoch();
}

bool LLRecentPeople::save() const
{
    if (mFilename.empty()) return false;

    llofstream file(mFilename);
    if (!file.is_open())
        return false;

    for (auto const& item : mPeople)
    {
        if (item.second.has("nearby") && item.second["nearby"].asBoolean()) continue;

        file << LLSDOStreamer<LLSDNotationFormatter>(item.second) << std::endl;
    }

    file.close();
    return true;
}

bool LLRecentPeople::load()
{
    LL_INFOS("RecentPeople") << "Loading recent storage" << LL_ENDL;
    if (mFilename.empty()) return false;

    llifstream file(mFilename);

    mPeople.clear();

    // add each line in the file to the list
    std::string line;
    LLPointer<LLSDParser> parser = new LLSDNotationParser();
    while (std::getline(file, line))
    {
        LLSD s_item;
        std::istringstream iss(line);
        if (parser->parse(iss, s_item, line.length()) == LLSDParser::PARSE_FAILURE) break;

        // No id? wtf is going on here? skip this shit.
        if (!s_item.has("id")) break;

        static LLCachedControl<S32> sRecentPeopleMaxAge(gSavedSettings, "AlchemyRecentPeopleMaxAge", 30);
        if (s_item["date"].asDate().secondsSinceEpoch() > LLDate::now().secondsSinceEpoch() - sRecentPeopleMaxAge * 86400)
        {
            LLUUID id = (s_item["id"].asUUID());
            mPeople.insert(std::make_pair(id, s_item));
        }
    }

    file.close();
    mChangedSignal();
    return true;
}

void LLRecentPeople::clearHistory()
{
    mPeople.clear();
    mChangedSignal();
    save();
}
