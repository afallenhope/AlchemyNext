/**
 * @file streamingaudio_fmodstudio.cpp
 * @brief LLStreamingAudio_FMODSTUDIO implementation
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

#include "linden_common.h"
#include "llstreamingaudio_fmodstudio.h"

#include "llsd.h"
#include "llmath.h"
#include "llmutex.h"

#include "fmod.hpp"
#include "fmod_errors.h"

inline bool Check_FMOD_Stream_Error(FMOD_RESULT result, const char *string)
{
    if (result == FMOD_OK)
        return false;
    LL_WARNS("AudioImpl") << string << " Error: " << FMOD_ErrorString(result) << LL_ENDL;
    return true;
}

class LLAudioStreamManagerFMODSTUDIO
{
public:
    LLAudioStreamManagerFMODSTUDIO(FMOD::System *system, FMOD::ChannelGroup *group, const std::string& url);
    FMOD::Channel* startStream();
    bool stopStream(); // Returns true if the stream was successfully stopped.

    const std::string& getURL()     { return mInternetStreamURL; }

    FMOD_RESULT getOpenState(FMOD_OPENSTATE& openstate, unsigned int* percentbuffered = nullptr, bool* starving = nullptr, bool* diskbusy = nullptr);
protected:
    FMOD::System* mSystem;
    FMOD::ChannelGroup* mChannelGroup;
    FMOD::Channel* mStreamChannel;
    FMOD::Sound* mInternetStream;
    bool mReady;

    std::string mInternetStreamURL;
};

LLMutex gWaveDataMutex;    //Just to be extra strict.
const U32 WAVE_BUFFER_SIZE = 1024;
U32 gWaveBufferMinSize = 0;
F32 gWaveDataBuffer[WAVE_BUFFER_SIZE] = { 0.f };
U32 gWaveDataBufferSize = 0;

FMOD_RESULT F_CALLBACK waveDataCallback(FMOD_DSP_STATE *dsp_state, float *inbuffer, float *outbuffer, unsigned int length, int inchannels, int *outchannels)
{
    if (!length || !inchannels)
        return FMOD_OK;
    memcpy(outbuffer, inbuffer, length * inchannels * sizeof(float));

    static std::vector<F32> local_buf;
    if (local_buf.size() < length)
        local_buf.resize(length, 0.f);

    for (U32 i = 0; i < length; ++i)
    {
        F32 total = 0.f;
        for (S32 j = 0; j < inchannels; ++j)
        {
            total += inbuffer[i*inchannels + j];
        }
        local_buf[i] = total / inchannels;
    }

    {
        LLMutexLock lock(&gWaveDataMutex);

        for (U32 i = length; i > 0; --i)
        {
            if (++gWaveDataBufferSize > WAVE_BUFFER_SIZE)
            {
                if (gWaveBufferMinSize)
                    memcpy(gWaveDataBuffer + WAVE_BUFFER_SIZE - gWaveBufferMinSize, gWaveDataBuffer, gWaveBufferMinSize * sizeof(float));
                gWaveDataBufferSize = 1 + gWaveBufferMinSize;
            }
            gWaveDataBuffer[WAVE_BUFFER_SIZE - gWaveDataBufferSize] = local_buf[i - 1];
        }
    }

    return FMOD_OK;
}

//---------------------------------------------------------------------------
// Internet Streaming
//---------------------------------------------------------------------------
LLStreamingAudio_FMODSTUDIO::LLStreamingAudio_FMODSTUDIO(FMOD::System *system) :
    mSystem(system),
    mCurrentInternetStreamp(nullptr),
    mStreamDSP(nullptr),
    mStreamGroup(nullptr),
    mFMODInternetStreamChannelp(nullptr),
    mGain(1.0f),
    mWasAlreadyPlaying(false),
    mMetadata(LLSD::emptyMap())
{
    // Number of milliseconds of audio to buffer for the audio card.
    // Must be larger than the usual Second Life frame stutter time.
    const U32 buffer_seconds = 10;      //sec
    const U32 estimated_bitrate = 128;  //kbit/sec
    Check_FMOD_Stream_Error(mSystem->setStreamBufferSize(estimated_bitrate * buffer_seconds * 128/*bytes/kbit*/, FMOD_TIMEUNIT_RAWBYTES), "FMOD::System::setStreamBufferSize");

    Check_FMOD_Stream_Error(system->createChannelGroup("stream", &mStreamGroup), "FMOD::System::createChannelGroup");

    FMOD_DSP_DESCRIPTION dspdesc = { };
    dspdesc.pluginsdkversion = FMOD_PLUGIN_SDK_VERSION;
    strncpy(dspdesc.name, "Waveform", sizeof(dspdesc.name));
    dspdesc.numoutputbuffers = 1;
    dspdesc.read = &waveDataCallback; //Assign callback.

    Check_FMOD_Stream_Error(system->createDSP(&dspdesc, &mStreamDSP), "FMOD::System::createDSP");
}

LLStreamingAudio_FMODSTUDIO::~LLStreamingAudio_FMODSTUDIO()
{
    stop();
    for (U32 i = 0; i < 100; ++i)
    {
        if (releaseDeadStreams())
            break;
        ms_sleep(10);
    }

    cleanupWaveData();
}

void LLStreamingAudio_FMODSTUDIO::start(const std::string& url)
{
    //if (!mInited)
    //{
    //  LL_WARNS() << "startInternetStream before audio initialized" << LL_ENDL;
    //  return;
    //}

    // "stop" stream but don't clear url, etc. in case url == mInternetStreamURL
    stop();

    if (!url.empty())
    {
        if (mDeadStreams.empty())
        {
            LL_INFOS() << "Starting internet stream: " << url << LL_ENDL;
            mCurrentInternetStreamp = new LLAudioStreamManagerFMODSTUDIO(mSystem, mStreamGroup, url);
            mURL = url;
        }
        else
        {
            LL_INFOS() << "Deferring stream load until buffer release: " << url << LL_ENDL;
            mPendingURL = url;
        }
    }
    else
    {
        LL_INFOS() << "Set internet stream to null" << LL_ENDL;
        mURL.clear();
    }
}

enum utf_endian_type_t
{
    UTF16LE,
    UTF16BE,
    UTF16
};

std::string utf16input_to_utf8(unsigned char* input, U32 len, utf_endian_type_t type)
{
    if (type == UTF16)
    {
        type = UTF16BE;    //Default
        if (len > 2)
        {
            //Parse and strip BOM.
            if ((input[0] == 0xFE && input[1] == 0xFF) ||
                (input[0] == 0xFF && input[1] == 0xFE))
            {
                input += 2;
                len -= 2;
                type = input[0] == 0xFE ? UTF16BE : UTF16LE;
            }
        }
    }
    llutf16string out_16((llutf16string::value_type*)input, len / 2);
    if (len % 2)
    {
        out_16.push_back((input)[len - 1] << 8);
    }
    if (type == UTF16BE)
    {
        for (llutf16string::iterator i = out_16.begin(); i < out_16.end(); ++i)
        {
            llutf16string::value_type v = *i;
            *i = ((v & 0x00FF) << 8) | ((v & 0xFF00) >> 8);
        }
    }
    return utf16str_to_utf8str(out_16);
}

void LLStreamingAudio_FMODSTUDIO::update()
{
    if (!releaseDeadStreams())
    {
        llassert_always(mCurrentInternetStreamp == NULL);
        return;
    }

    if (!mPendingURL.empty())
    {
        llassert_always(mCurrentInternetStreamp == NULL);
        LL_INFOS() << "Starting internet stream: " << mPendingURL << LL_ENDL;
        mCurrentInternetStreamp = new LLAudioStreamManagerFMODSTUDIO(mSystem, mStreamGroup, mPendingURL);
        mURL = mPendingURL;
        mMetadata = LLSD::emptyMap();
        mMetadataUpdateSignal(mMetadata);
        mPendingURL.clear();
    }

    // Don't do anything if there are no streams playing
    if (!mCurrentInternetStreamp)
    {
        return;
    }

    unsigned int progress;
    bool starving;
    bool diskbusy;
    FMOD_OPENSTATE open_state;

    if (Check_FMOD_Stream_Error(mCurrentInternetStreamp->getOpenState(open_state, &progress, &starving, &diskbusy), "FMOD::Sound::getOpenState")  || open_state == FMOD_OPENSTATE_ERROR)
    {
        LL_WARNS() << "Internet stream openstate error: open_state = " << open_state << " - progress = " << progress << " - starving = " << starving << " - diskbusy = " << diskbusy << LL_ENDL;
        bool was_playing = mWasAlreadyPlaying;
        stop();
        // Try to restart previously playing stream on socket error
        if (open_state == FMOD_OPENSTATE_ERROR && was_playing)
        {
            LL_WARNS() << "Stream was playing before - trying to restart" << LL_ENDL;
            start(mURL);
        }
        return;
    }
    else if (open_state == FMOD_OPENSTATE_READY)
    {
        // Stream is live

        // start the stream if it's ready
        if (!mFMODInternetStreamChannelp &&
            (mFMODInternetStreamChannelp = mCurrentInternetStreamp->startStream()))
        {
            // Reset volume to previously set volume
            setGain(getGain());
            if (mStreamDSP)
            {
                Check_FMOD_Stream_Error(mFMODInternetStreamChannelp->addDSP(FMOD_CHANNELCONTROL_DSP_TAIL, mStreamDSP), "FMOD::Channel::addDSP");
            }
            Check_FMOD_Stream_Error(mFMODInternetStreamChannelp->setPaused(false), "FMOD::Channel::setPaused");
            mWasAlreadyPlaying = true;
        }
    }
    else if (open_state == FMOD_OPENSTATE_PLAYING)
    {
        if (!mWasAlreadyPlaying)
        {
            mWasAlreadyPlaying = true;
        }
    }


    if (mFMODInternetStreamChannelp)
    {
        FMOD::Sound *sound = nullptr;

        if(mFMODInternetStreamChannelp->getCurrentSound(&sound) == FMOD_OK && sound)
        {
            FMOD_TAG tag;
            S32 tagcount, dirtytagcount;
            if(sound->getNumTags(&tagcount, &dirtytagcount) == FMOD_OK && dirtytagcount > 0)
            {
                mMetadata = LLSD::emptyMap();

                for(S32 i = 0; i < tagcount; ++i)
                {
                    if(sound->getTag(nullptr, i, &tag)!=FMOD_OK)
                        continue;

                    std::string name = tag.name;
                    switch (tag.type)
                    {
                        case FMOD_TAGTYPE_ID3V2:
                        {
                            if (!LLStringUtil::compareInsensitive(name, "TIT2")) name = "TITLE";
                            else if(!LLStringUtil::compareInsensitive(name, "TPE1")) name = "ARTIST";
                            break;
                        }
                        case FMOD_TAGTYPE_ASF:
                        {
                            if (!LLStringUtil::compareInsensitive(name, "Title")) name = "TITLE";
                            else if (!LLStringUtil::compareInsensitive(name, "WM/AlbumArtist")) name = "ARTIST";
                            break;
                        }
                        case FMOD_TAGTYPE_VORBISCOMMENT:
                        {
                                if (!LLStringUtil::compareInsensitive(name, "title")) name = "TITLE";
                                else if (!LLStringUtil::compareInsensitive(name, "artist")) name = "ARTIST";
                            break;
                        }
                        case FMOD_TAGTYPE_FMOD:
                        {
                            if (!LLStringUtil::compareInsensitive(name, "Sample Rate Change"))
                            {
                                LL_INFOS() << "Stream forced changing sample rate to " << *((float *)tag.data) << LL_ENDL;
                                Check_FMOD_Stream_Error(mFMODInternetStreamChannelp->setFrequency(*((float *)tag.data)), "FMOD::Channel::setFrequency");
                            }
                            continue;
                        }
                        default:
                            if (!LLStringUtil::compareInsensitive(name, "TITLE") ||
                                !LLStringUtil::compareInsensitive(name, "ARTIST"))
                                LLStringUtil::toUpper(name);
                            break;
                    }
                    switch (tag.datatype)
                    {
                        case(FMOD_TAGDATATYPE_INT):
                           mMetadata[name]=*(LLSD::Integer*)(tag.data);
                            LL_INFOS() << tag.name << ": " << *(int*)(tag.data) << LL_ENDL;
                            break;
                        case(FMOD_TAGDATATYPE_FLOAT):
                            mMetadata[name]=*(LLSD::Real*)(tag.data);
                            LL_INFOS() << tag.name << ": " << *(float*)(tag.data) << LL_ENDL;
                            break;
                        case(FMOD_TAGDATATYPE_STRING):
                        {
                            std::string out = rawstr_to_utf8(std::string((char*)tag.data,tag.datalen));
                            if (out.length() && out[out.size() - 1] == 0)
                                out.erase(out.size() - 1);
                            mMetadata[name]=out;
                            LL_INFOS() << tag.name << "(RAW): " << out << LL_ENDL;
                            break;
                        }
                        case(FMOD_TAGDATATYPE_STRING_UTF8) :
                        {
                            U8 offs = 0;
                            if (tag.datalen > 3 && ((unsigned char*)tag.data)[0] == 0xEF && ((unsigned char*)tag.data)[1] == 0xBB && ((unsigned char*)tag.data)[2] == 0xBF)
                                offs = 3;
                            std::string out((char*)tag.data + offs, tag.datalen - offs);
                            if (out.length() && out[out.size() - 1] == 0)
                                out.erase(out.size() - 1);
                            mMetadata[name] = out;
                            LL_INFOS() << tag.name << "(UTF8): " << out << LL_ENDL;
                            break;
                        }
                        case(FMOD_TAGDATATYPE_STRING_UTF16):
                        {
                            std::string out = utf16input_to_utf8((unsigned char*)tag.data, tag.datalen, UTF16);
                            if (out.length() && out[out.size() - 1] == 0)
                                out.erase(out.size() - 1);
                            mMetadata[name] = out;
                            LL_INFOS() << tag.name << "(UTF16): " << out << LL_ENDL;
                            break;
                        }
                        case(FMOD_TAGDATATYPE_STRING_UTF16BE):
                        {
                            std::string out = utf16input_to_utf8((unsigned char*)tag.data, tag.datalen, UTF16BE);
                            if (out.length() && out[out.size() - 1] == 0)
                                out.erase(out.size() - 1);
                            mMetadata[name] = out;
                            LL_INFOS() << tag.name << "(UTF16BE): " << out << LL_ENDL;
                            break;
                        }
                        default:
                            break;
                    }
                }

                mMetadataUpdateSignal(mMetadata);
            }

            if (starving)
            {
                bool paused = false;
                if (!Check_FMOD_Stream_Error(mFMODInternetStreamChannelp->getPaused(&paused), "FMOD:Channel::getPaused") && !paused)
                {
                    LL_INFOS() << "Stream starvation detected! Pausing stream until buffer nearly full." << LL_ENDL;
                    LL_INFOS() << "  (diskbusy=" << diskbusy << ")" << LL_ENDL;
                    LL_INFOS() << "  (progress=" << progress << ")" << LL_ENDL;
                    Check_FMOD_Stream_Error(mFMODInternetStreamChannelp->setPaused(true), "FMOD::Channel::setPaused");
                }
            }
            else if(progress > 80)
            {
                Check_FMOD_Stream_Error(mFMODInternetStreamChannelp->setPaused(false), "FMOD::Channel::setPaused");
            }
        }
    }
}

void LLStreamingAudio_FMODSTUDIO::stop()
{
    mPendingURL.clear();
    mWasAlreadyPlaying = false;

    mMetadata = LLSD::emptyMap();
    mMetadataUpdateSignal(mMetadata);

    if (mFMODInternetStreamChannelp)
    {
        Check_FMOD_Stream_Error(mFMODInternetStreamChannelp->setPaused(true), "FMOD::Channel::setPaused");
        Check_FMOD_Stream_Error(mFMODInternetStreamChannelp->setPriority(0), "FMOD::Channel::setPriority");
        if (mStreamDSP)
        {
            Check_FMOD_Stream_Error(mFMODInternetStreamChannelp->removeDSP(mStreamDSP), "FMOD::Channel::removeDSP");
        }
        mFMODInternetStreamChannelp = nullptr;
    }

    if (mCurrentInternetStreamp)
    {
        LL_INFOS("FMOD") << "Stopping internet stream: " << mCurrentInternetStreamp->getURL() << LL_ENDL;
        if (mCurrentInternetStreamp->stopStream())
        {
            delete mCurrentInternetStreamp;
        }
        else
        {
            LL_WARNS("FMOD") << "Pushing stream to dead list: " << mCurrentInternetStreamp->getURL() << LL_ENDL;
            mDeadStreams.push_back(mCurrentInternetStreamp);
        }
        mCurrentInternetStreamp = nullptr;
    }
}

void LLStreamingAudio_FMODSTUDIO::pause(S32 pauseopt)
{
    if (pauseopt < 0)
    {
        pauseopt = mCurrentInternetStreamp ? 1 : 0;
    }

    if (pauseopt)
    {
        if (mCurrentInternetStreamp)
        {
            stop();
        }
    }
    else
    {
        start(getURL());
    }
}


// A stream is "playing" if it has been requested to start.  That
// doesn't necessarily mean audio is coming out of the speakers.
int LLStreamingAudio_FMODSTUDIO::isPlaying()
{
    if (mCurrentInternetStreamp)
    {
        return 1; // Active and playing
    }
    else if (!mURL.empty() || !mPendingURL.empty())
    {
        return 2; // "Paused"
    }
    else
    {
        return 0;
    }
}


F32 LLStreamingAudio_FMODSTUDIO::getGain()
{
    return mGain;
}


std::string LLStreamingAudio_FMODSTUDIO::getURL()
{
    return mURL;
}


void LLStreamingAudio_FMODSTUDIO::setGain(F32 vol)
{
    mGain = vol;

    if (mFMODInternetStreamChannelp)
    {
        vol = llclamp(vol * vol, 0.f, 1.f); //should vol be squared here?

        Check_FMOD_Stream_Error(mFMODInternetStreamChannelp->setVolume(vol), "FMOD::Channel::setVolume");
    }
}

/* virtual */
bool LLStreamingAudio_FMODSTUDIO::getWaveData(float* arr, S32 count, S32 stride/*=1*/)
{
    if (count > (WAVE_BUFFER_SIZE / 2))
        LL_ERRS("AudioImpl") << "Count=" << count << " exceeds WAVE_BUFFER_SIZE/2=" << WAVE_BUFFER_SIZE << LL_ENDL;

    if(!mFMODInternetStreamChannelp || !mCurrentInternetStreamp)
        return false;

    bool muted = false;
    FMOD_RESULT res = mFMODInternetStreamChannelp->getMute(&muted);
    if(res != FMOD_OK || muted)
        return false;
    {
        U32 buff_size;
        {
            LLMutexLock lock(&gWaveDataMutex);
            gWaveBufferMinSize = count;
            buff_size = gWaveDataBufferSize;
            if (!buff_size)
                return false;
            memcpy(arr, gWaveDataBuffer + WAVE_BUFFER_SIZE - buff_size, llmin(U32(count), buff_size) * sizeof(float));
        }
        if (buff_size < U32(count))
            memset(arr + buff_size, 0, (count - buff_size) * sizeof(float));
    }
    return true;
}

///////////////////////////////////////////////////////
// manager of possibly-multiple internet audio streams

LLAudioStreamManagerFMODSTUDIO::LLAudioStreamManagerFMODSTUDIO(FMOD::System *system, FMOD::ChannelGroup *group, const std::string& url) :
    mSystem(system),
    mChannelGroup(group),
    mStreamChannel(nullptr),
    mInternetStream(nullptr),
    mReady(false),
    mInternetStreamURL(url)
{
    FMOD_RESULT result = mSystem->createStream(url.c_str(), FMOD_2D | FMOD_NONBLOCKING | FMOD_IGNORETAGS, nullptr, &mInternetStream);

    if (result != FMOD_OK)
    {
        LL_WARNS("FMOD") << "Couldn't open fmod stream, error "
            << FMOD_ErrorString(result)
            << LL_ENDL;
        mReady = false;
        return;
    }

    mReady = true;
}

FMOD::Channel *LLAudioStreamManagerFMODSTUDIO::startStream()
{
    // We need a live and opened stream before we try and play it.
    FMOD_OPENSTATE open_state;
    if (!mInternetStream || Check_FMOD_Stream_Error(getOpenState(open_state), "FMOD::Sound::getOpenState") || open_state != FMOD_OPENSTATE_READY)
    {
        LL_WARNS("FMOD") << "No internet stream to start playing!" << LL_ENDL;
        return nullptr;
    }

    if (mStreamChannel)
        return mStreamChannel;  //Already have a channel for this stream.

    Check_FMOD_Stream_Error(mSystem->playSound(mInternetStream, mChannelGroup, true, &mStreamChannel), "FMOD::System::playSound");
    return mStreamChannel;
}

bool LLAudioStreamManagerFMODSTUDIO::stopStream()
{
    if (mInternetStream)
    {
        bool close = true;
        FMOD_OPENSTATE open_state;
        if (getOpenState(open_state) == FMOD_OK)
        {
            switch (open_state)
            {
            case FMOD_OPENSTATE_CONNECTING:
                close = false;
                break;
            default:
                close = true;
            }
        }

        if (close && mInternetStream->release() == FMOD_OK)
        {
            mStreamChannel = nullptr;
            mInternetStream = nullptr;
            return true;
        }
        else
        {
            return false;
        }
    }
    else
    {
        return true;
    }
}

FMOD_RESULT LLAudioStreamManagerFMODSTUDIO::getOpenState(FMOD_OPENSTATE& state, unsigned int* percentbuffered, bool* starving, bool* diskbusy)
{
    if (!mInternetStream)
        return FMOD_ERR_INVALID_HANDLE;
    FMOD_RESULT result = mInternetStream->getOpenState(&state, percentbuffered, starving, diskbusy);
    Check_FMOD_Stream_Error(result, "FMOD::Sound::getOpenState");
    return result;
}

void LLStreamingAudio_FMODSTUDIO::setBufferSizes(U32 streambuffertime, U32 decodebuffertime)
{
    Check_FMOD_Stream_Error(mSystem->setStreamBufferSize(streambuffertime / 1000 * 128 * 128, FMOD_TIMEUNIT_RAWBYTES), "FMOD::System::setStreamBufferSize");
    FMOD_ADVANCEDSETTINGS settings = { };
    settings.cbSize = sizeof(settings);
    settings.defaultDecodeBufferSize = decodebuffertime;//ms
    Check_FMOD_Stream_Error(mSystem->setAdvancedSettings(&settings), "FMOD::System::setAdvancedSettings");
}

bool LLStreamingAudio_FMODSTUDIO::releaseDeadStreams()
{
    // Kill dead internet streams, if possible
    for (auto iter = mDeadStreams.begin(); iter != mDeadStreams.end();)
    {
        LLAudioStreamManagerFMODSTUDIO *streamp = *iter;
        if (streamp->stopStream())
        {
            LL_INFOS() << "Closed dead stream" << LL_ENDL;
            delete streamp;
            iter = mDeadStreams.erase(iter);
        }
        else
        {
            ++iter;
        }
    }

    return mDeadStreams.empty();
}

void LLStreamingAudio_FMODSTUDIO::cleanupWaveData()
{
    if (mStreamGroup)
    {
        Check_FMOD_Stream_Error(mStreamGroup->release(), "FMOD::ChannelGroup::release");
        mStreamGroup = nullptr;
    }

    if(mStreamDSP)
        Check_FMOD_Stream_Error(mStreamDSP->release(), "FMOD::DSP::release");
    mStreamDSP = nullptr;
}
