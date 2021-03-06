#include "ofxMultiSpeakerSoundPlayer.h"
#include "ofUtils.h"


bool bFmodInitialized_ = false;
bool bUseSpectrum_ = false;
float fftValues_[8192];			//
float fftInterpValues_[8192];			//
float fftSpectrum_[8192];		// maximum #ofxMultiSpeakerSoundPlayer is 8192, in fmodex....


// ---------------------  static vars
static FMOD_CHANNELGROUP * channelgroup;
static FMOD_SYSTEM       * sys;

// these are global functions, that affect every sound / channel:
// ------------------------------------------------------------
// ------------------------------------------------------------

//--------------------
void fmodStopAll()
{
    ofxMultiSpeakerSoundPlayer::initializeFmod();
    FMOD_ChannelGroup_Stop(channelgroup);
}

//--------------------
void fmodSetVolume(float vol)
{
    ofxMultiSpeakerSoundPlayer::initializeFmod();
    FMOD_ChannelGroup_SetVolume(channelgroup, vol);
}

//--------------------
void fmodSoundUpdate()
{
    if (bFmodInitialized_)
    {
        FMOD_System_Update(sys);
    }
}

//--------------------
float * fmodSoundGetSpectrum(int nBands)
{

    ofxMultiSpeakerSoundPlayer::initializeFmod();


    // 	set to 0
    for (int i = 0; i < 8192; i++)
    {
        fftInterpValues_[i] = 0;
    }

    // 	check what the user wants vs. what we can do:
    if (nBands > 8192)
    {
        ofLogWarning("ofxMultiSpeakerSoundPlayer") << "ofFmodGetSpectrum(): requested number of bands " << nBands << ", using maximum of 8192";
        nBands = 8192;
    }
    else if (nBands <= 0)
    {
        ofLogWarning("ofxMultiSpeakerSoundPlayer") << "ofFmodGetSpectrum(): requested number of bands " << nBands << ", using minimum of 1";
        nBands = 1;
        return fftInterpValues_;
    }

    // 	FMOD needs pow2
    int nBandsToGet = ofNextPow2(nBands);
    if (nBandsToGet < 64) nBandsToGet = 64;  // can't seem to get fft of 32, etc from fmodex

    // 	get the fft
    FMOD_System_GetSpectrum(sys, fftSpectrum_, nBandsToGet, 0, FMOD_DSP_FFT_WINDOW_HANNING);

    // 	convert to db scale
    for(int i = 0; i < nBandsToGet; i++)
    {
        fftValues_[i] = 10.0f * (float)log10(1 + fftSpectrum_[i]) * 2.0f;
    }

    // 	try to put all of the values (nBandsToGet) into (nBands)
    //  in a way which is accurate and preserves the data:
    //

    if (nBandsToGet == nBands)
    {

        for(int i = 0; i < nBandsToGet; i++)
        {
            fftInterpValues_[i] = fftValues_[i];
        }

    }
    else
    {

        float step 		= (float)nBandsToGet / (float)nBands;
        //float pos 		= 0;
        // so for example, if nBands = 33, nBandsToGet = 64, step = 1.93f;
        int currentBand = 0;

        for(int i = 0; i < nBandsToGet; i++)
        {

            // if I am current band = 0, I care about (0+1) * step, my end pos
            // if i > endPos, then split i with me and my neighbor

            if (i >= ((currentBand+1)*step))
            {

                // do some fractional thing here...
                float fraction = ((currentBand+1)*step) - (i-1);
                float one_m_fraction = 1 - fraction;
                fftInterpValues_[currentBand] += fraction * fftValues_[i];
                currentBand++;
                // safety check:
                if (currentBand >= nBands)
                {
                    ofLogWarning("ofxMultiSpeakerSoundPlayer") << "ofFmodGetSpectrum(): currentBand >= nBands";
                }

                fftInterpValues_[currentBand] += one_m_fraction * fftValues_[i];

            }
            else
            {
                // do normal things
                fftInterpValues_[currentBand] += fftValues_[i];
            }
        }

        // because we added "step" amount per band, divide to get the mean:
        for (int i = 0; i < nBands; i++)
        {
            fftInterpValues_[i] /= step;
            if (fftInterpValues_[i] > 1)fftInterpValues_[i] = 1; 	// this seems "wrong"
        }

    }

    return fftInterpValues_;
}

// ------------------------------------------------------------
// ------------------------------------------------------------


// now, the individual sound player:
//------------------------------------------------------------
ofxMultiSpeakerSoundPlayer::ofxMultiSpeakerSoundPlayer()
{
    bLoop 			= false;
    bLoadedOk 		= false;
    pan 			= 0.0f; // range for oF is -1 to 1
    volume 			= 1.0f;
    internalFreq 	= 44100;
    speed 			= 1;
    bPaused 		= false;
    isStreaming		= false;
}


//---------------------------------------
// this should only be called once
void ofxMultiSpeakerSoundPlayer::initializeFmod()
{
    FMOD_SPEAKERMODE  speakermode;
    int numdrivers;

    if(!bFmodInitialized_)
    {
        FMOD_System_Create(&sys);
        FMOD_System_SetDriver(sys, 7); // setup driver 7.1 with index 7
        FMOD_System_SetSpeakerMode(sys, FMOD_SPEAKERMODE_7POINT1);

#ifdef TARGET_LINUX
        FMOD_System_SetOutput(sys,FMOD_OUTPUTTYPE_ALSA);
#endif

        FMOD_System_Init(sys, 32, FMOD_INIT_NORMAL, NULL);  //do we want just 32 channels?
        FMOD_System_GetMasterChannelGroup(sys, &channelgroup);
        bFmodInitialized_ = true;
    }
}


//---------------------------------------
void ofxMultiSpeakerSoundPlayer::closeFmod()
{
    if(bFmodInitialized_)
    {
        FMOD_System_Close(sys);
        bFmodInitialized_ = false;
    }
}

//------------------------------------------------------------
bool ofxMultiSpeakerSoundPlayer::load(string fileName, bool stream)
{
	currentLoaded = fileName;

    fileName = ofToDataPath(fileName);

    // fmod uses IO posix internally, might have trouble
    // with unicode paths...
    // says this code:
    // http://66.102.9.104/search?q=cache:LM47mq8hytwJ:www.cleeker.com/doxygen/audioengine__fmod_8cpp-source.html+FSOUND_Sample_Load+cpp&hl=en&ct=clnk&cd=18&client=firefox-a
    // for now we use FMODs way, but we could switch if
    // there are problems:

    bMultiPlay = false;

    // [1] init fmod, if necessary

    initializeFmod();

    // [2] try to unload any previously loaded sounds
    // & prevent user-created memory leaks
    // if they call "loadSound" repeatedly, for example

    unload();

    // [3] load sound

    //choose if we want streaming
    int fmodFlags =  FMOD_SOFTWARE;
    if(stream)fmodFlags =  FMOD_SOFTWARE | FMOD_CREATESTREAM;

    result = FMOD_System_CreateSound(sys, fileName.c_str(),  fmodFlags, NULL, &sound);

    if (result != FMOD_OK)
    {
        bLoadedOk = false;
        ofLogError("ofxMultiSpeakerSoundPlayer") << "loadSound(): could not load \"" << fileName << "\"";
    }
    else
    {
        bLoadedOk = true;
        FMOD_Sound_GetLength(sound, &length, FMOD_TIMEUNIT_PCM);
        isStreaming = stream;
    }

    return bLoadedOk;
}

//------------------------------------------------------------
void ofxMultiSpeakerSoundPlayer::unload()
{
    if (bLoadedOk)
    {
        stop();						// try to stop the sound
        if(!isStreaming)FMOD_Sound_Release(sound);
        bLoadedOk = false;
    }
}

//------------------------------------------------------------
bool ofxMultiSpeakerSoundPlayer::isPlaying() const
{

    if (!bLoadedOk) return false;

    int playing = 0;
    FMOD_Channel_IsPlaying(channel, &playing);
    return (playing != 0 ? true : false);
}

//------------------------------------------------------------
float ofxMultiSpeakerSoundPlayer::getSpeed() const
{
    return speed;
}

//------------------------------------------------------------
float ofxMultiSpeakerSoundPlayer::getPan() const
{
    return pan;
}

//------------------------------------------------------------
float ofxMultiSpeakerSoundPlayer::getVolume() const 
{
    return volume;
}

//------------------------------------------------------------
bool ofxMultiSpeakerSoundPlayer::isLoaded() const
{
    return bLoadedOk;
}

//------------------------------------------------------------
void ofxMultiSpeakerSoundPlayer::setVolume(float vol)
{
    if (isPlaying() == true)
    {
        FMOD_Channel_SetVolume(channel, vol);
    }
    volume = vol;
}

//------------------------------------------------------------
void ofxMultiSpeakerSoundPlayer::setPosition(float pct)
{
    if (isPlaying() == true)
    {
        int sampleToBeAt = (int)(length * pct);
        FMOD_Channel_SetPosition(channel, sampleToBeAt, FMOD_TIMEUNIT_PCM);
    }
}

void ofxMultiSpeakerSoundPlayer::setPositionMS(int ms)
{
    if (isPlaying() == true)
    {
        FMOD_Channel_SetPosition(channel, ms, FMOD_TIMEUNIT_MS);
    }
}

//------------------------------------------------------------
float ofxMultiSpeakerSoundPlayer::getPosition() const
{
    if (isPlaying() == true)
    {
        unsigned int sampleImAt;

        FMOD_Channel_GetPosition(channel, &sampleImAt, FMOD_TIMEUNIT_PCM);

        float pct = 0.0f;
        if (length > 0)
        {
            pct = sampleImAt / (float)length;
        }
        return pct;
    }
    else
    {
        return 0;
    }
}

//------------------------------------------------------------
int ofxMultiSpeakerSoundPlayer::getPositionMS() const
{
    if (isPlaying() == true)
    {
        unsigned int sampleImAt;

        FMOD_Channel_GetPosition(channel, &sampleImAt, FMOD_TIMEUNIT_MS);

        return sampleImAt;
    }
    else
    {
        return 0;
    }
}

//------------------------------------------------------------
void ofxMultiSpeakerSoundPlayer::setPan(float p)
{
    pan = p;
    p = ofClamp(p, -1, 1);
    if (isPlaying() == true)
    {
        FMOD_Channel_SetPan(channel,p);
    }
}


//------------------------------------------------------------
void ofxMultiSpeakerSoundPlayer::setPaused(bool bP)
{
    if (isPlaying() == true)
    {
        FMOD_Channel_SetPaused(channel,bP);
        bPaused = bP;
    }
}


//------------------------------------------------------------
void ofxMultiSpeakerSoundPlayer::setSpeed(float spd)
{
    if (isPlaying() == true)
    {
        FMOD_Channel_SetFrequency(channel, internalFreq * spd);
    }
    speed = spd;
}


//------------------------------------------------------------
void ofxMultiSpeakerSoundPlayer::setLoop(bool bLp)
{
    if (isPlaying() == true)
    {
        FMOD_Channel_SetMode(channel,  (bLp == true) ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF);
    }
    bLoop = bLp;
}

// ----------------------------------------------------------------------------
void ofxMultiSpeakerSoundPlayer::setMultiPlay(bool bMp)
{
    bMultiPlay = bMp;		// be careful with this...
}

// ----------------------------------------------------------------------------
void ofxMultiSpeakerSoundPlayer::play()
{

    // if it's a looping sound, we should try to kill it, no?
    // or else people will have orphan channels that are looping
    if (bLoop == true)
    {
        FMOD_Channel_Stop(channel);
    }

    // if the sound is not set to multiplay, then stop the current,
    // before we start another
    if (!bMultiPlay)
    {
        FMOD_Channel_Stop(channel);
    }

    FMOD_System_PlaySound(sys, FMOD_CHANNEL_FREE, sound, bPaused, &channel);

    FMOD_Channel_GetFrequency(channel, &internalFreq);
    FMOD_Channel_SetVolume(channel,volume);
    setPan(pan);
    FMOD_Channel_SetFrequency(channel, internalFreq * speed);
    FMOD_Channel_SetMode(channel,  (bLoop == true) ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF);

    //fmod update() should be called every frame - according to the docs.
    //we have been using fmod without calling it at all which resulted in channels not being able
    //to be reused.  we should have some sort of global update function but putting it here
    //solves the channel bug
    FMOD_System_Update(sys);

}

// ----------------------------------------------------------------------------
void ofxMultiSpeakerSoundPlayer::playTo(int speaker)
{

    // if it's a looping sound, we should try to kill it, no?
    // or else people will have orphan channels that are looping
    if (bLoop == true)
    {
        FMOD_Channel_Stop(channel);
    }

    // if the sound is not set to multiplay, then stop the current,
    // before we start another
    if (!bMultiPlay)
    {
        FMOD_Channel_Stop(channel);
    }

    FMOD_System_PlaySound(sys, FMOD_CHANNEL_FREE, sound, 1, &channel);

    /*
    FMOD_RESULT Channel::setSpeakerMix(
      float  frontleft,
      float  frontright,
      float  center,
      float  lfe,
      float  backleft,
      float  backright,
      float  sideleft,
      float  sideright
    );
    */


    switch (speaker)
    {
    case 0 : // send to back output
        FMOD_Channel_SetSpeakerMix(channel, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0);
        break;
    case 1 : // send to side output
        FMOD_Channel_SetSpeakerMix(channel, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0);
        break;
    case 2 : // send to front output
        FMOD_Channel_SetSpeakerMix(channel, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
        break;
    case 3 : // send to center output
        FMOD_Channel_SetSpeakerMix(channel, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0);
        break;
    }

    FMOD_Channel_SetPaused(channel, 0);

    FMOD_Channel_GetFrequency(channel, &internalFreq);
    FMOD_Channel_SetVolume(channel,volume);
    FMOD_Channel_SetFrequency(channel, internalFreq * speed);
    FMOD_Channel_SetMode(channel,  (bLoop == true) ? FMOD_LOOP_NORMAL : FMOD_LOOP_OFF);

    //fmod update() should be called every frame - according to the docs.
    //we have been using fmod without calling it at all which resulted in channels not being able
    //to be reused.  we should have some sort of global update function but putting it here
    //solves the channel bug
    FMOD_System_Update(sys);

}

// ----------------------------------------------------------------------------
void ofxMultiSpeakerSoundPlayer::stop()
{
    FMOD_Channel_Stop(channel);
}

