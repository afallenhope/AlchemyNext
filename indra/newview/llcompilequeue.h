/**
 * @file llcompilequeue.h
 * @brief LLCompileQueue class header file
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
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

#ifndef LL_LLCOMPILEQUEUE_H
#define LL_LLCOMPILEQUEUE_H

#include "llinventory.h"
#include "llviewerobject.h"
#include "lluuid.h"

#include "llfloater.h"

#include "llevents.h"

class LLScrollListCtrl;
class FSLSLPreprocessor;

struct LLScriptQueueData
{
    LLUUID mQueueID;
    LLUUID mTaskId;
    LLPointer<LLInventoryItem> mItem;
    LLUUID mExperienceId;
    std::string mExperiencename;

    LLScriptQueueData(const LLUUID& q_id, const LLUUID& task_id, const LLUUID& experience_id, LLInventoryItem* item) :
        mQueueID(q_id),
        mTaskId(task_id),
        mExperienceId(experience_id),
        mItem(new LLInventoryItem(item))
    { }

};
// </FS:KC>

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Class LLFloaterScriptQueue
//
// This class provides a mechanism of adding objects to a list that
// will go through and execute action for the scripts on each object. The
// objects will be accessed serially and the scripts may be
// manipulated in parallel. For example, selecting two objects each
// with three scripts will result in the first object having all three
// scripts manipulated.
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

class LLFloaterScriptQueue : public LLFloater/*, public LLVOInventoryListener*/
{
public:
    LLFloaterScriptQueue(const LLSD& key);
    virtual ~LLFloaterScriptQueue();

    /*virtual*/ BOOL postBuild() override;

    void setMono(bool mono) { mMono = mono; }

    // addObject() accepts an object id.
    void addObject(const LLUUID& id, std::string name);

    // start() returns TRUE if the queue has started, otherwise FALSE.
    BOOL start();

    void addProcessingMessage(const std::string &message, const LLSD &args);
    void addStringMessage(const std::string &message);

    std::string getStartString() const { return mStartString; }

protected:
    static void onCloseBtn(void* user_data);

    // returns true if this is done
    BOOL isDone() const;

    virtual bool startQueue() = 0;

    void setStartString(const std::string& s) { mStartString = s; }

protected:
    // UI
    LLScrollListCtrl* mMessages;
    LLButton* mCloseBtn;

    // Object Queue
    struct ObjectData
    {
        LLUUID mObjectId;
        std::string mObjectName;
    };
    typedef std::vector<ObjectData> object_data_list_t;

    object_data_list_t mObjectList;
    LLUUID mCurrentObjectID;
    bool mDone;

    std::string mStartString;
    bool mMono;

    typedef boost::function<bool(const LLPointer<LLViewerObject> &, LLInventoryObject*, LLEventPump &)>   fnQueueAction_t;
    static void objectScriptProcessingQueueCoro(std::string action, LLHandle<LLFloaterScriptQueue> hfloater, object_data_list_t objectList, fnQueueAction_t func);

};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Class LLFloaterCompileQueue
//
// This script queue will recompile each script.
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

struct LLCompileQueueData
{
    LLUUID mQueueID;
    LLUUID mItemId;
    LLCompileQueueData(const LLUUID& q_id, const LLUUID& item_id) :
        mQueueID(q_id), mItemId(item_id) {}
};

class LLFloaterCompileQueue final : public LLFloaterScriptQueue
{
    friend class LLFloaterReg;
public:

    void experienceIdsReceived( const LLSD& content );
    BOOL hasExperience(const LLUUID& id)const;

    static void finishLSLUpload(LLUUID itemId, LLUUID taskId, LLUUID newAssetId, LLSD response, std::string scriptName, LLUUID queueId);
    static void scriptPreprocComplete(const LLUUID& asset_id, LLScriptQueueData* data, LLAssetType::EType type, const std::string& script_text);
    static void scriptLogMessage(LLScriptQueueData* data, std::string message);
protected:
    LLFloaterCompileQueue(const LLSD& key);
    virtual ~LLFloaterCompileQueue();

    bool startQueue() override;

    static bool processScript(LLHandle<LLFloaterCompileQueue> hfloater, const LLPointer<LLViewerObject> &object, LLInventoryObject* inventory, LLEventPump &pump);

    //bool checkAssetId(const LLUUID &assetId);
    static void handleHTTPResponse(std::string pumpName, const LLSD &expresult);
    static void handleScriptRetrieval(const LLUUID& assetId, LLAssetType::EType type, void* userData, S32 status, LLExtStat extStatus);

private:
    static void processExperienceIdResults(LLSD result, LLUUID parent);
    //uuid_list_t mAssetIds;  // list of asset IDs processed.
    uuid_list_t mExperienceIds;

    std::unique_ptr<FSLSLPreprocessor> mLSLProc;
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Class LLFloaterResetQueue
//
// This script queue will reset each script.
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

class LLFloaterResetQueue final : public LLFloaterScriptQueue
{
    friend class LLFloaterReg;
protected:
    LLFloaterResetQueue(const LLSD& key);
    virtual ~LLFloaterResetQueue();

    static bool resetObjectScripts(LLHandle<LLFloaterScriptQueue> hfloater, const LLPointer<LLViewerObject> &object, LLInventoryObject* inventory, LLEventPump &pump);

    bool startQueue() override;
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Class LLFloaterRunQueue
//
// This script queue will set each script as running.
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

class LLFloaterRunQueue final : public LLFloaterScriptQueue
{
    friend class LLFloaterReg;
protected:
    LLFloaterRunQueue(const LLSD& key);
    virtual ~LLFloaterRunQueue();

    static bool runObjectScripts(LLHandle<LLFloaterScriptQueue> hfloater, const LLPointer<LLViewerObject> &object, LLInventoryObject* inventory, LLEventPump &pump);

    bool startQueue() override;
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Class LLFloaterNotRunQueue
//
// This script queue will set each script as not running.
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

class LLFloaterNotRunQueue final : public LLFloaterScriptQueue
{
    friend class LLFloaterReg;
protected:
    LLFloaterNotRunQueue(const LLSD& key);
    virtual ~LLFloaterNotRunQueue();

    static bool stopObjectScripts(LLHandle<LLFloaterScriptQueue> hfloater, const LLPointer<LLViewerObject> &object, LLInventoryObject* inventory, LLEventPump &pump);

    bool startQueue() override;
};

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Class LLFloaterDeleteQueue
//
// This script queue will remove each script.
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

class LLFloaterDeleteQueue final : public LLFloaterScriptQueue
{
    friend class LLFloaterReg;
protected:
    LLFloaterDeleteQueue(const LLSD& key);
    virtual ~LLFloaterDeleteQueue();

    static bool deleteObjectScripts(LLHandle<LLFloaterScriptQueue> hfloater, const LLPointer<LLViewerObject> &object, LLInventoryObject* inventory, LLEventPump &pump);

    bool startQueue() override;
};
#endif // LL_LLCOMPILEQUEUE_H
