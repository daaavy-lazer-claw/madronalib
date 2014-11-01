
// MadronaLib: a C++ framework for DSP applications.
// Copyright (c) 2013 Madrona Labs LLC. http://www.madronalabs.com
// Distributed under the MIT license: http://madrona-labs.mit-license.org/

#ifndef __PLUGINPROCESSOR__
#define __PLUGINPROCESSOR__

#include "MLDSPEngine.h"
#include "MLAudioProcessorListener.h"
#include "MLDefaultFileLocations.h"
#include "MLModel.h"
#include "MLPluginEditor.h"
#include "MLFileCollection.h"
#include "MLControlEvent.h"
#include "MLProperty.h" 
#include "MLAppState.h"

#include <vector>
#include <map>

const int kMLPatcherMaxTableSize = 64;

class MLPluginProcessor : 
	public AudioProcessor,
    public MLFileCollection::Listener,
	public MLModel
{
public:
    
    enum
    {
        kRequiresSSE2,
        kRequiresSSE3
    };
    
	MLPluginProcessor();
    ~MLPluginProcessor();

	// MLModel implementation
	void doPropertyChangeAction(MLSymbol property, const MLProperty& newVal);
	
	// juce::AudioProcessor implementation
	const String getName() const { return MLProjectInfo::projectName; }
    void prepareToPlay (double sampleRate, int samplesPerBlock);
    void releaseResources();
    void processBlock (AudioSampleBuffer& buffer, MidiBuffer& midiMessages);
    const String getInputChannelName (const int channelIndex) const;
    const String getOutputChannelName (const int channelIndex) const;
    bool isInputChannelStereoPair (int index) const;
    bool isOutputChannelStereoPair (int index) const;
	double getTailLengthSeconds() const;
	bool acceptsMidi() const;
    bool producesMidi() const;
	void reset();
	AudioProcessorEditor* createEditor();
	bool hasEditor() const { return true; }
	int getNumParameters();
    const String getParameterName (int index);
	float getParameter (int index);
    const String getParameterText (int index);
	float getParameterDefaultValue (int index);
    void setParameter (int index, float newValue);
	//  TODO bool isParameterAutomatable (int parameterIndex) const;
	// factory presets - a VST concept - unimplemented
    int getNumPrograms() { return 0; }
    int getCurrentProgram() { return 0; }
    void setCurrentProgram (int) { }
    const String getProgramName (int) { return String::empty; }
    void changeProgramName (int, const String&) { }
	void getStateInformation (juce::MemoryBlock& destData);
    void setStateInformation (const void* data, int sizeInBytes);
    
    void editorResized(int w, int h);
    
	// plugin description and default preset
	void loadPluginDescription(const char* desc);
	virtual void loadDefaultPreset() = 0;
	
	// initializeProcessor is called after graph is created.
	virtual void initializeProcessor() = 0; 

	// preflight and cleanup
	MLProc::err preflight(int requirements = kRequiresSSE2);
	virtual bool wantsMIDI() {return true;}
	void setDefaultParameters();

    // MLFileCollection::Listener
	void processFileFromCollection (const MLFile& file, const MLFileCollection& collection, int idx, int size);
	
	// add an additional listener to the file collections that we are listening to. Controllers can use this
	// to get updates and build menus, etc.
	void addFileCollectionListener(MLFileCollection::Listener* pL);

	// process
	bool isOKToProcess();
    void convertMIDIToEvents (MidiBuffer& midiMessages, MLControlEventVector & events);
	void setCollectStats(bool k);

	// parameters
	int getParameterIndex (const MLSymbol name);
	float getParameterAsLinearProportion (int index);
	void setParameterAsLinearProportion (int index, float newValue);
	
	const String symbolToXMLAttr(const MLSymbol sym);
	const MLSymbol XMLAttrToSymbol(const String& str);
	
    const MLSymbol getParameterAlias (int index);
    float getParameterMin (int index);
	float getParameterMax (int index);
	
	MLPublishedParamPtr getParameterPtr (int index);
	MLPublishedParamPtr getParameterPtr (MLSymbol sym);
	const std::string& getParameterGroupName (int index);

	// signals
	int countSignals(const MLSymbol alias);

	// state
	// REMOVING
	virtual void getStateAsXML (XmlElement& xml);
	int saveStateAsVersion();
    
	int saveStateOverPrevious();
	void returnToLatestStateLoaded();
	
	String getStateAsText ();
	void setStateFromText (const String& stateStr);

    int saveStateToFullPath(const std::string& path);
    void saveStateToRelativePath(const std::string& path);
    
    void setStateFromPath(const std::string& path);
	void setStateFromMIDIProgram (const int pgmIdx);
	void setStateFromFile(const File& loadFile);
	virtual void setStateFromXML(const XmlElement& xmlState, bool setViewAttributes);
 
	// files
	void createFileCollections();
	void scanAllFilesImmediate();
		
	// presets
    void prevPreset();
    void nextPreset();
    void advancePreset(int amount);
	
    AudioPlayHead::CurrentPositionInfo lastPosInfo;

	void setMLListener (MLAudioProcessorListener* const newListener) throw();
    MLProc::err sendMessageToMLListener (unsigned msg, const File& f);

	// scales
	void loadScale(const File& f);
	void loadDefaultScale();
	virtual void broadcastScale(const MLScale* pScale) = 0;
	
	// engine stuff
	MLDSPEngine* getEngine() { return &mEngine; }
	inline void showEngine() { mEngine.dump(); }
	
protected:
	// set what kind of event input we are listening to (MIDI or OSC)
	void setInputProtocol(int p);

	// set the parameter of the Engine but not the Model property.
	void setParameterWithoutProperty (MLSymbol paramName, float newValue);
	void setSignalParameterWithoutProperty (MLSymbol paramName, const MLSignal& newValue);
	
	// Engine creates graphs of Processors, does the work
	MLDSPEngine	mEngine;	
	
	typedef std::tr1::shared_ptr<XmlElement> XmlElementPtr;
	XmlElementPtr mpLatestStateLoaded;

	// TODO shared_ptr
	ScopedPointer<XmlDocument> mpPluginDoc;
	String mDocLocationString;

	// input protocol stuff
	int mInputProtocol;
	int mT3DWaitTime;
    osc::int32 mDataRate;
	
private:
	class ProtocolPoller : public Timer
	{
		ProtocolPoller(MLPluginProcessor*);
		~ProtocolPoller();
		void timerCallback();
	};
	
	void setCurrentPresetDir(const char* name);
    
	// set the plugin state from a memory blob containing parameter and patcher settings.
	void setStateFromBlob (const void* data, int sizeInBytes);

	MLAudioProcessorListener* mMLListener;
    
	// The number of parameters in the plugin is stored here so we can access it before
	// the DSP engine is compiled.
	int mNumParameters; 
	
	// True when any parameters have been set by the host. 
	// If the host doesn't give us a program to load, we can use this to 
	// decide to load defaults after compile.
	bool mHasParametersSet;

	// temp storage for parameter data given to us before our DSP graph is made.
	juce::MemoryBlock mSavedParamBlob;	
	
	String mCurrentPresetName;
	String mCurrentPresetDir;

    MLFileCollectionPtr mScaleFiles;
    MLFileCollectionPtr mPresetFiles;
    MLFileCollectionPtr mMIDIProgramFiles;
	
	// saved state for editor
	MLRect mEditorRect;
	bool mEditorNumbersOn;
	bool mEditorAnimationsOn;
	
	bool mInitialized;
    
    // vector of control events to send to engine along with each block of audio.
    MLControlEventVector mControlEvents;

	int mTemp;
	
	MLAppState* mpState;

};

#endif  // __PLUGINPROCESSOR__
