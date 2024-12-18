/*
 * @file llpanelsearchweb.cpp
 * @brief Web search panel
 *
 * Copyright (c) 2014, Cinder Roxley <cinder@sdf.org>
 *
 * Permission is hereby granted, free of charge, to any person or organization
 * obtaining a copy of the software and accompanying documentation covered by
 * this license (the "Software") to use, reproduce, display, distribute,
 * execute, and transmit the Software, and to prepare derivative works of the
 * Software, and to permit third-parties to whom the Software is furnished to
 * do so, all subject to the following:
 *
 * The copyright notices in the Software and this entire statement, including
 * the above license grant, this restriction and the following disclaimer,
 * must be included in all copies of the Software, in whole or in part, and
 * all derivative works of the Software, unless such copies or derivative
 * works are solely in the form of machine-executable object code generated by
 * a source language processor.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
 * SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
 * FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

#include "llviewerprecompiledheaders.h"
#include "llpanelsearchweb.h"

#include "llagent.h"
#include "llfloaterdirectory.h"
#include "lllogininstance.h"
#include "llmediactrl.h"
#include "llprogressbar.h"
#include "lltextbox.h"
#include "llviewercontrol.h"
#include "llviewernetwork.h"
#include "llviewerregion.h"
#include "llweb.h"

static LLPanelInjector<LLPanelSearchWeb> t_panel_search_web("panel_search_web");

LLPanelSearchWeb::LLPanelSearchWeb()
:   LLPanel()
,   mStatusBarText(nullptr)
,   mStatusBarProgress(nullptr)
,   mBtnBack(nullptr)
,   mBtnForward(nullptr)
,   mBtnReload(nullptr)
,   mBtnStop(nullptr)
,   mWebBrowser(nullptr)
{
    mSearchType.insert("standard");
    mSearchType.insert("land");
    mSearchType.insert("classified");

    mCollectionType.insert("events");
    mCollectionType.insert("destinations");
    mCollectionType.insert("places");
    mCollectionType.insert("groups");
    mCollectionType.insert("people");

    mCategoryPaths = LLSD::emptyMap();
    mCategoryPaths["all"] = "search";
    mCategoryPaths["people"] = "search/people";
    mCategoryPaths["places"] = "search/places";
    mCategoryPaths["events"] = "search/events";
    mCategoryPaths["groups"] = "search/groups";
    mCategoryPaths["wiki"] = "search/wiki";
    mCategoryPaths["destinations"] = "destinations";

    mCommitCallbackRegistrar.add("WebContent.Back", boost::bind(&LLPanelSearchWeb::onClickBack, this));
    mCommitCallbackRegistrar.add("WebContent.Forward", boost::bind(&LLPanelSearchWeb::onClickForward, this));
    mCommitCallbackRegistrar.add("WebContent.Reload", boost::bind(&LLPanelSearchWeb::onClickReload, this));
    mCommitCallbackRegistrar.add("WebContent.Stop", boost::bind(&LLPanelSearchWeb::onClickStop, this));
}

BOOL LLPanelSearchWeb::postBuild()
{
    mWebBrowser = getChild<LLMediaCtrl>("webbrowser");
    if (mWebBrowser) mWebBrowser->addObserver(this);

    mWebBrowser        = getChild<LLMediaCtrl>("webbrowser");
    mStatusBarProgress = getChild<LLProgressBar>("statusbarprogress");
    mStatusBarText     = getChild<LLTextBox>("statusbartext");

    mBtnBack           = getChild<LLButton>("back");
    mBtnForward        = getChild<LLButton>("forward");
    mBtnReload         = getChild<LLButton>("reload");
    mBtnStop           = getChild<LLButton>("stop");

    return TRUE;
}

void LLPanelSearchWeb::loadUrl(const SearchQuery& p)
{
    if (!mWebBrowser || !p.validateBlock()) return;

    mWebBrowser->setTrustedContent(true);

    // work out the subdir to use based on the requested category
    LLSD subs;
    if (LLGridManager::instance().isInSecondlife())
    {
        if (mSearchType.find(p.category.getValue()) != mSearchType.end())
        {
            subs["TYPE"] = p.category.getValue();
        }
        else
        {
            subs["TYPE"] = "standard";
        }

        subs["COLLECTION"] = "";
        if (subs["TYPE"] == "standard")
        {
            if (mCollectionType.find(p.collection) != mCollectionType.end())
            {
                subs["COLLECTION"] = "&collection_chosen=" + std::string(p.collection);
            }
            else
            {
                std::string collection_args("");
                for (std::set<std::string>::iterator it = mCollectionType.begin(); it != mCollectionType.end(); ++it)
                {
                    collection_args += "&collection_chosen=" + std::string(*it);
                }
                subs["COLLECTION"] = collection_args;
            }
        }
    }
    else
    {
        subs["CATEGORY"] = (mCategoryPaths.has(p.category.getValue())
            ? mCategoryPaths[p.category.getValue()].asString()
            : mCategoryPaths["all"].asString());
    }

    // add the search query string
    subs["QUERY"] = LLURI::escape(p.query());

    // add the user's preferred maturity (can be changed via prefs)
    std::string maturity;
    if (LLGridManager::instance().isInSecondlife())
    {
        if (gAgent.prefersAdult())
        {
            maturity = "gma";  // PG,Mature,Adult
        }
        else if (gAgent.prefersMature())
        {
            maturity = "gm";  // PG,Mature
        }
        else
        {
            maturity = "g";  // PG
        }
    }
    else
    {
        if (gAgent.prefersAdult())
        {
            maturity = "42";  // PG,Mature,Adult
        }
        else if (gAgent.prefersMature())
        {
            maturity = "21";  // PG,Mature
        }
        else
        {
            maturity = "13";  // PG
        }
    }
    subs["MATURITY"] = maturity;

    // add the user's god status
    subs["GODLIKE"] = gAgent.isGodlike() ? "1" : "0";

    // Get the search URL and expand all of the substitutions
    // (also adds things like [LANGUAGE], [VERSION], [OS], etc.)
    LLViewerRegion* regionp = gAgent.getRegion();
    std::string url = regionp != nullptr ? regionp->getSearchServerURL()
        : gSavedSettings.getString(LLGridManager::getInstance()->isInOpenSim() ? "OpenSimSearchURL" : "SearchURL");
    url = LLWeb::expandURLSubstitutions(url, subs);
    // Finally, load the URL in the webpanel
    mWebBrowser->navigateTo(url, "text/html");
}

// virtual
void LLPanelSearchWeb::handleMediaEvent(LLPluginClassMedia* self, EMediaEvent event)
{
    if(event == MEDIA_EVENT_LOCATION_CHANGED)
    {
        const std::string url = self->getLocation();

        if ( url.length() )
            mStatusBarText->setText( url );
    }
    else if(event == MEDIA_EVENT_NAVIGATE_BEGIN)
    {
        // flags are sent with this event
        mBtnBack->setEnabled( self->getHistoryBackAvailable() );
        mBtnForward->setEnabled( self->getHistoryForwardAvailable() );

        // toggle visibility of these buttons based on browser state
        mBtnReload->setVisible( false );
        mBtnStop->setVisible( true );

        // turn "on" progress bar now we're about to start loading
        mStatusBarProgress->setVisible( true );
    }
    else if(event == MEDIA_EVENT_NAVIGATE_COMPLETE)
    {
        // flags are sent with this event
        mBtnBack->setEnabled( self->getHistoryBackAvailable() );
        mBtnForward->setEnabled( self->getHistoryForwardAvailable() );

        // toggle visibility of these buttons based on browser state
        mBtnReload->setVisible( true );
        mBtnStop->setVisible( false );

        // turn "off" progress bar now we're loaded
        mStatusBarProgress->setVisible( false );

        // we populate the status bar with URLs as they change so clear it now we're done
        const LLStringExplicit end_str("");
        mStatusBarText->setText( end_str );
    }
    else if(event == MEDIA_EVENT_STATUS_TEXT_CHANGED )
    {
        const std::string text = self->getStatusText();
        if ( text.length() )
            mStatusBarText->setText( text );
    }
    else if(event == MEDIA_EVENT_PROGRESS_UPDATED )
    {
        int percent = (int)self->getProgressPercent();
        mStatusBarProgress->setValue( percent );
    }
    else if(event == MEDIA_EVENT_LINK_HOVERED )
    {
        const std::string link = self->getHoverLink();
        mStatusBarText->setText( link );
    }
}

void LLPanelSearchWeb::onClickForward() const
{
    mWebBrowser->navigateForward();
}

void LLPanelSearchWeb::onClickBack() const
{
    mWebBrowser->navigateBack();
}

void LLPanelSearchWeb::onClickReload() const
{

    if( mWebBrowser->getMediaPlugin() )
    {
        bool ignore_cache = true;
        mWebBrowser->getMediaPlugin()->browse_reload( ignore_cache );
    }
}

void LLPanelSearchWeb::onClickStop()
{
    if( mWebBrowser->getMediaPlugin() )
        mWebBrowser->getMediaPlugin()->browse_stop();

    // still should happen when we catch the navigate complete event
    // but sometimes (don't know why) that event isn't sent from Qt
    // and we ghetto a point where the stop button stays active.
    mBtnReload->setVisible( true );
    mBtnStop->setVisible( false );
}
