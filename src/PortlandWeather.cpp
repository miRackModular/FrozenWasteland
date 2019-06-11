#include "FrozenWasteland.hpp"
#include "ui/knobs.hpp"
#include "ringbuffer.hpp"
#include "samplerate.h"
#include "StateVariableFilter.h"
#include "clouds/dsp/frame.h"
#include "clouds/dsp/fx/pitch_shifter.h"
#include <iostream>

#define HISTORY_SIZE (1<<22)
#define MAX_GRAIN_SIZE (1<<16)	
#define NUM_TAPS 16
#define MAX_GRAINS 4
#define CHANNELS 2
#define DIVISIONS 36
#define NUM_GROOVES 16


struct PortlandWeather : Module {
	typedef float T;

	struct LowFrequencyOscillator {
		float phase = 0.0;
		float freq = 1.0;
		bool invert = false;

		//void setFrequency(float frequency) {
		//	freq = frequency;
		//}

		void hardReset()
		{
			phase = 0.0;
		}

		void reset()
		{
			phase -= 1.0;
		}


		void step(float dt) {
			float deltaPhase = fminf(freq * dt, 0.5);
			phase += deltaPhase;
			//if (phase >= 1.0)
			//	phase -= 1.0;
		}
		float sin() {
			return sinf(2*M_PI * phase) * (invert ? -1.0 : 1.0);
		}
		float progress() {
			return phase;
		}
	};

	enum ParamIds {
		CLOCK_DIV_PARAM,
		TIME_PARAM,
		GRID_PARAM,
		GROOVE_TYPE_PARAM,
		GROOVE_AMOUNT_PARAM,
		GRAIN_QUANTITY_PARAM,
		GRAIN_SIZE_PARAM,
		FEEDBACK_PARAM,
		FEEDBACK_TAP_L_PARAM,
		FEEDBACK_TAP_R_PARAM,
		FEEDBACK_L_SLIP_PARAM,
		FEEDBACK_R_SLIP_PARAM,
		FEEDBACK_TONE_PARAM,
		FEEDBACK_L_PITCH_SHIFT_PARAM,
		FEEDBACK_R_PITCH_SHIFT_PARAM,
		FEEDBACK_L_DETUNE_PARAM,
		FEEDBACK_R_DETUNE_PARAM,		
		PING_PONG_PARAM,
		REVERSE_PARAM,
		MIX_PARAM,
		TAP_MUTE_PARAM,
		TAP_STACKED_PARAM = TAP_MUTE_PARAM+NUM_TAPS,
		TAP_MIX_PARAM = TAP_STACKED_PARAM+NUM_TAPS,
		TAP_PAN_PARAM = TAP_MIX_PARAM+NUM_TAPS,
		TAP_FILTER_TYPE_PARAM = TAP_PAN_PARAM+NUM_TAPS,
		TAP_FC_PARAM = TAP_FILTER_TYPE_PARAM+NUM_TAPS,
		TAP_Q_PARAM = TAP_FC_PARAM+NUM_TAPS,
		TAP_PITCH_SHIFT_PARAM = TAP_Q_PARAM+NUM_TAPS,
		TAP_DETUNE_PARAM = TAP_PITCH_SHIFT_PARAM+NUM_TAPS,
		CLEAR_BUFFER_PARAM = TAP_DETUNE_PARAM+NUM_TAPS,
		NUM_PARAMS
	};
	enum InputIds {
		CLOCK_INPUT,
		CLOCK_DIVISION_CV_INPUT,
		TIME_CV_INPUT,
		EXTERNAL_DELAY_TIME_INPUT,
		GRID_CV_INPUT,
		GROOVE_TYPE_CV_INPUT,
		GROOVE_AMOUNT_CV_INPUT,
		FEEDBACK_INPUT,
		FEEDBACK_TAP_L_INPUT,
		FEEDBACK_TAP_R_INPUT,
		FEEDBACK_TONE_INPUT,
		FEEDBACK_L_SLIP_CV_INPUT,
		FEEDBACK_R_SLIP_CV_INPUT,
		FEEDBACK_L_PITCH_SHIFT_CV_INPUT,
		FEEDBACK_R_PITCH_SHIFT_CV_INPUT,
		FEEDBACK_L_DETUNE_CV_INPUT,
		FEEDBACK_R_DETUNE_CV_INPUT,
		FEEDBACK_L_RETURN,
		FEEDBACK_R_RETURN,
		PING_PONG_INPUT,
		REVERSE_INPUT,
		MIX_INPUT,
		TAP_MUTE_CV_INPUT,
		TAP_STACK_CV_INPUT = TAP_MUTE_CV_INPUT + NUM_TAPS,
		TAP_MIX_CV_INPUT = TAP_STACK_CV_INPUT + NUM_TAPS,
		TAP_PAN_CV_INPUT = TAP_MIX_CV_INPUT + NUM_TAPS,
		TAP_FC_CV_INPUT = TAP_PAN_CV_INPUT + NUM_TAPS,
		TAP_Q_CV_INPUT = TAP_FC_CV_INPUT + NUM_TAPS,
		TAP_PITCH_SHIFT_CV_INPUT = TAP_Q_CV_INPUT + NUM_TAPS,
		TAP_DETUNE_CV_INPUT = TAP_PITCH_SHIFT_CV_INPUT + NUM_TAPS,
		IN_L_INPUT = TAP_DETUNE_CV_INPUT+NUM_TAPS,
		IN_R_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUT_L_OUTPUT,
		OUT_R_OUTPUT,
		FEEDBACK_L_OUTPUT,
		FEEDBACK_R_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		PING_PONG_LIGHT,
		REVERSE_LIGHT,
		TAP_MUTED_LIGHT,
		TAP_STACKED_LIGHT = TAP_MUTED_LIGHT+NUM_TAPS,
		FREQ_LIGHT = TAP_STACKED_LIGHT+NUM_TAPS,
		NUM_LIGHTS
	};
	enum FilterModes {
		FILTER_NONE,
		FILTER_LOWPASS,
		FILTER_HIGHPASS,
		FILTER_BANDPASS,
		FILTER_NOTCH
	};



	const char* grooveNames[NUM_GROOVES] = {"Straight","Swing","Hard Swing","Reverse Swing","Alternate Swing","Accelerando","Ritardando","Waltz Time","Half Swing","Roller Coaster","Quintuple","Random 1","Random 2","Random 3","Early Reflection","Late Reflection"};
	const float tapGroovePatterns[NUM_GROOVES][NUM_TAPS] = {
		{1.0f,2.0f,3.0,4.0,5.0,6.0,7.0,8.0,9.0,10.0,11.0,12.0,13.0,14.0,15.0,16.0f}, // Straight time
		{1.25,2.0,3.25,4.0,5.25,6.0,7.25,8.0,9.25,10.0,11.25,12.0,13.25,14.0,15.25,16.0}, // Swing
		{1.75,2.0,3.75,4.0,5.75,6.0,7.75,8.0,9.75,10.0,11.75,12.0,13.75,14.0,15.75,16.0}, // Hard Swing
		{0.75,2.0,2.75,4.0,4.75,6.0,6.75,8.0,8.75,10.0,10.75,12.0,12.75,14.0,14.75,16.0}, // Reverse Swing
		{1.25,2.0,3.0,4.0,5.25,6.0,7.0,8.0,9.25,10.0,11.0,12.0,13.25,14.0,15.0,16.0}, // Alternate Swing
		{3.0,5.0,7.0,9.0,10.0,11.0,12.0,13.0,13.5,14.0,14.5,15.0,15.25,15.5,15.75,16.0}, // Accelerando
		{0.25,0.5,0.75,1.0,1.5,2.0,2.5,3.0,4.0,5.0,6.0,7.0,9.0,11.0,13.0,16.0}, // Ritardando
		{1.25,2.75,3.25,4.0,5.25,6.75,7.25,8.0,9.25,10.75,11.25,12.0,13.25,14.75,15.25,16.0}, // Waltz Time
		{1.5,2.0,3.5,4.0,5.0,6.0,7.0,8.0,9.5,10.0,11.5,12.0,13.0,14.0,15.0,16.0}, // Half Swing
		{1.0,2.0,4.0,5.0,6.0,8.0,10.0,12.0,12.5,13.0,13.5,14.0,14.5,15.0,15.5,16.0}, // Roller Coaster
		{1.75,2.5,3.25,4.0,4.75,6.5,7.25,8.0,9.75,10.5,11.25,12.0,12.75,14.5,15.25,16.0}, // Quintuple
		{0.25,0.75,1.0,1.25,4.0,5.5,7.25,7.5,8.0,8.25,10.0,11.0,13.5,15.0,15.75,16.0}, // Uniform Random 1
		{0.25,4.75,5.25,5.5,7.0,8.0,8.5,8.75,9.0,9.25,11.75,12.75,13.0,13.25,14.75,15.5}, // Uniform Random 2
		{0.75,2.0,2.25,5.75,7.25,7.5,7.75,8.5,8.75,12.5,12.75,13.0,13.75,14.0,14.5,16.0}, // Uniform Random 3
		{0.25,0.5,1.0,1.25,1.75,2.0,2.5,3.5,4.25,4.5,4.75,5,6.25,8.25,11.0,16.0}, // Early Reflection
		{7.0,7.25,9.0,9.25,10.25,12.5,13.0,13.75,14.0,15.0,15.25,15.5,15.75,16.0,16.0,16.0} // Late Reflection
	};

	const float minCutoff = 15.0;
	const float maxCutoff = 8400.0;

	int tapGroovePattern = 0;
	float grooveAmount = 1.0f;

	bool pingPong = false;
	bool reverse = false;
	int grainNumbers = 1;
	bool tapMuted[NUM_TAPS+1];
	bool tapStacked[NUM_TAPS+1];
	int lastFilterType[NUM_TAPS+1];
	float lastTapFc[NUM_TAPS+1];
	float lastTapQ[NUM_TAPS+1];
	float tapPitchShift[NUM_TAPS+1];
	float tapDetune[NUM_TAPS+1];
	int tapFilterType[NUM_TAPS+1];
	int feedbackTap[CHANNELS] = {NUM_TAPS-1,NUM_TAPS-1};
	float feedbackSlip[CHANNELS] = {0.0f,0.0f};
	float feedbackPitch[CHANNELS] = {0.0f,0.0f};
	float feedbackDetune[CHANNELS] = {0.0f,0.0f};
	float delayTime[NUM_TAPS+1][CHANNELS];
	float actualDelayTime[NUM_TAPS+1][CHANNELS][2];
	float initialWindowedOutput[NUM_TAPS+1][CHANNELS][2];
	
	StateVariableFilterState<T> filterStates[NUM_TAPS][CHANNELS];
    StateVariableFilterParams<T> filterParams[NUM_TAPS];
	dsp::RCFilter lowpassFilter[CHANNELS];
	dsp::RCFilter highpassFilter[CHANNELS];

	const char* filterNames[5] = {"OFF","LP","HP","BP","NOTCH"};

	const char* tapNames[NUM_TAPS+2] {"1","2","3","4","5","6","7","8","9","10","11","12","13","14","15","16","ALL","EXT"};
	const char* grainNames[MAX_GRAINS] {"1","2","4","Raw"};

	
	clouds::PitchShifter pitch_shifter_[NUM_TAPS+1][CHANNELS][MAX_GRAINS];
	dsp::SchmittTrigger clockTrigger,pingPongTrigger,reverseTrigger,clearBufferTrigger,mutingTrigger[NUM_TAPS],stackingTrigger[NUM_TAPS];
	float divisions[DIVISIONS] = {1/256.0,1/192.0,1/128.0,1/96.0,1/64.0,1/48.0,1/32.0,1/24.0,1/16.0,1/13.0,1/12.0,1/11.0,1/8.0,1/7.0,1/6.0,1/5.0,1/4.0,1/3.0,1/2.0,1/1.5,1,1/1.5,2.0,3.0,4.0,5.0,6.0,7.0,8.0,9.0,10.0,11.0,12.0,13.0,16.0,24.0};
	const char* divisionNames[DIVISIONS] = {"/256","/192","/128","/96","/64","/48","/32","/24","/16","/13","/12","/11","/8","/7","/6","/5","/4","/3","/2","/1.5","x 1","x 1.5","x 2","x 3","x 4","x 5","x 6","x 7","x 8","x 9","x 10","x 11","x 12","x 13","x 16","x 24"};
	int division = 0;
	float time = 0.0;
	float duration = 0;
	float baseDelay = 0.0;
	bool secondClockReceived = false;

	LowFrequencyOscillator sinOsc[2];
	FrozenWasteland::MultiTapDoubleRingBuffer<float, HISTORY_SIZE,NUM_TAPS+1> historyBuffer[CHANNELS][2];
	FrozenWasteland::ReverseRingBuffer<float, HISTORY_SIZE> reverseHistoryBuffer[CHANNELS];
	float pitchShiftBuffer[NUM_TAPS+1][CHANNELS][MAX_GRAINS][MAX_GRAIN_SIZE];
	clouds::FloatFrame pitchShiftOut_;
	FrozenWasteland::DoubleRingBuffer<float, 16> outBuffer[NUM_TAPS+1][CHANNELS][2]; 
	
	SRC_STATE *src;
	float lastFeedback[CHANNELS] = {0.0f,0.0f};

	float lerp(float v0, float v1, float t) {
	  return (1 - t) * v0 + t * v1;
	}

	float SemitonesToRatio(float semiTone) {
		return powf(2,semiTone/12.0f);
	}

	PortlandWeather() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		configParam(CLOCK_DIV_PARAM, 0, DIVISIONS-1, 0,"Divisions");
		configParam(TIME_PARAM, 0.0f, 10.0f, 0.350f,"Time"," ms",0,1000);
		//configParam(GRID_PARAM, 0.001f, 10.0f, 0.350f);

		configParam(GROOVE_TYPE_PARAM, 0.0f, 15.0f, 0.0f,"Groove Type");
		configParam(GROOVE_AMOUNT_PARAM, 0.0f, 1.0f, 1.0f,"Groove Amount","%",0,100);

		configParam(FEEDBACK_PARAM, 0.0f, 1.0f, 0.0f,"Feedback","%",0,100);
		configParam(FEEDBACK_TONE_PARAM, 0.0f, 1.0f, 0.5f,"Feedback Tone","%",0,100);

		configParam(FEEDBACK_TAP_L_PARAM, 0.0f, 17.0f, 15.0f,"Feedback L Tap");
		configParam(FEEDBACK_TAP_R_PARAM, 0.0f, 17.0f, 15.0f,"Feedback R Tap");
		configParam(FEEDBACK_L_SLIP_PARAM, -0.5f, 0.5f, 0.0f,"Feedback L Slip","%",0,100);
		configParam(FEEDBACK_R_SLIP_PARAM, -0.5f, 0.5f, 0.0f,"Feedback R Slip","%",0,100);
		configParam(FEEDBACK_L_PITCH_SHIFT_PARAM, -24.0f, 24.0f, 0.0f,"Feedback L Pitch Shift", " semitones");
		configParam(FEEDBACK_R_PITCH_SHIFT_PARAM, -24.0f, 24.0f, 0.0f,"Feedback R Pitch Shift", " semitones");
		configParam(FEEDBACK_L_DETUNE_PARAM, -99.0f, 99.0f, 0.0f,"Feedback L Detune", " cents");
		configParam(FEEDBACK_R_DETUNE_PARAM, -99.0f, 99.0f, 0.0f,"Feedback R Detune", " cents");

		configParam(GRAIN_QUANTITY_PARAM, 1, 4, 1,"# Grains");
		//configParam(GRAIN_SIZE_PARAM, 8, 11, 11);
		configParam(GRAIN_SIZE_PARAM, 0.0f, 1.0f, 1.0f,"Grain Size");
		

		configParam(CLEAR_BUFFER_PARAM, 0.0f, 1.0f, 0.0f);


		configParam(REVERSE_PARAM, 0.0f, 1.0f, 0.0f);
		
		configParam(PING_PONG_PARAM, 0.0f, 1.0f, 0.0f);
		
		//last tap isn't stacked
		for (int i = 0; i< NUM_TAPS-1; i++) {
			configParam(TAP_STACKED_PARAM + i, 0.0f, 1.0f, 0.0f);
		}

		for (int i = 0; i < NUM_TAPS; i++) {
			configParam(TAP_MUTE_PARAM + i, 0.0f, 1.0f, 0.0f);		
			configParam(TAP_MIX_PARAM + i, 0.0f, 1.0f, 0.5f,"Tap " + std::to_string(i+1) + " mix","%",0,100);
			configParam(TAP_PAN_PARAM + i, 0.0f, 1.0f, 0.5f,"Tap " + std::to_string(i+1) + " pan","%",0,100);
			configParam(TAP_FILTER_TYPE_PARAM + i, 0, 4, 0,"Tap " + std::to_string(i+1) + " filter type");
			configParam(TAP_FC_PARAM + i, 0.0f, 1.0f, 0.5f,"Tap " + std::to_string(i+1) + " Fc"," Hz",560,15);
			configParam(TAP_Q_PARAM + i, 0.01f, 1.0f, 0.5f,"Tap " + std::to_string(i+1) + " Q","%",0,100);
			configParam(TAP_PITCH_SHIFT_PARAM + i, -24.0f, 24.0f, 0.0f,"Tap " + std::to_string(i+1) + " pitch shift"," semitones");
			configParam(TAP_DETUNE_PARAM + i, -99.0f, 99.0f, 0.0f,"Tap " + std::to_string(i+1) + " detune"," cents");
		}

		configParam(MIX_PARAM, 0.0f, 1.0f, 0.5f,"Mix","%",0,100);

		//src = src_new(SRC_SINC_FASTEST, 1, NULL);
		src = src_new(SRC_ZERO_ORDER_HOLD, 1, NULL);

		sinOsc[1].phase = 0.25; //90 degrees out

		float sampleRate = APP->engine->getSampleRate();

		for (int i = 0; i <= NUM_TAPS; ++i) {
			tapMuted[i] = false;
			tapStacked[i] = false;
			tapPitchShift[i] = 0.0f;
			tapDetune[i] = 0.0f;
			lastFilterType[i] = FILTER_NONE;
			lastTapFc[i] = 800.0f / sampleRate;
			lastTapQ[i] = 5.0f;
			filterParams[i].setMode(StateVariableFilterParams<T>::Mode::LowPass);
			filterParams[i].setQ(5); 	
	        filterParams[i].setFreq(T(800.0f / sampleRate));

	        for(int j=0;j < CHANNELS;j++) {
				delayTime[i][j] = 0.0f;
	        	actualDelayTime[i][j][0] = 0.0f;
	        	actualDelayTime[i][j][1] = 0.0f;
	        	for(int k=0;k<MAX_GRAINS;k++) {
	    	 	   pitch_shifter_[i][j][k].Init(pitchShiftBuffer[i][j][k],k*0.25f);
	    		}
	    	}
	    }

	    //Initialize the feedback pitch shifters
        // for(int j=0;j < CHANNELS;j++) {
        // 	for(int k=0;k<MAX_GRAINS;k++) {
    	//  	   pitch_shifter_[NUM_TAPS][j][k].Init(pitchShiftBuffer[NUM_TAPS][j][k],k*0.25f);
    	// 	}
    	// }

	}



	json_t *dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "pingPong", json_integer((int) pingPong));

		json_object_set_new(rootJ, "reverse", json_integer((int) reverse));
		
		for(int i=0;i<NUM_TAPS;i++) {
			//This is so stupid!!! why did he not use strings?
			char buf[100];
			strcpy(buf, "muted");
			strcat(buf, tapNames[i]);
			json_object_set_new(rootJ, buf, json_integer((int) tapMuted[i]));

			strcpy(buf, "stacked");
			strcat(buf, tapNames[i]);
			json_object_set_new(rootJ, buf, json_integer((int) tapStacked[i]));
		}
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {

		json_t *sumJ = json_object_get(rootJ, "pingPong");
		if (sumJ) {
			pingPong = json_integer_value(sumJ);			
		}

		json_t *sumR = json_object_get(rootJ, "reverse");
		if (sumR) {
			reverse = json_integer_value(sumR);			
		}
		
		char buf[100];			
		for(int i=0;i<NUM_TAPS;i++) {
			strcpy(buf, "muted");
			strcat(buf, tapNames[i]);

			json_t *sumJ = json_object_get(rootJ, buf);
			if (sumJ) {
				tapMuted[i] = json_integer_value(sumJ);			
			}
		}

		for(int i=0;i<NUM_TAPS;i++) {
			strcpy(buf, "stacked");
			strcat(buf, tapNames[i]);

			json_t *sumJ = json_object_get(rootJ, buf);
			if (sumJ) {
				tapStacked[i] = json_integer_value(sumJ);
			}
		}
		
	}


	void process(const ProcessArgs &args) override;
};


void PortlandWeather::process(const ProcessArgs &args) {
	sinOsc[0].step(1.0 / args.sampleRate);
	sinOsc[1].step(1.0 / args.sampleRate);

	if (clearBufferTrigger.process(params[CLEAR_BUFFER_PARAM].getValue())) {
		for(int i=0;i<CHANNELS;i++) {
			historyBuffer[i][0].clear();
			historyBuffer[i][1].clear();
		}
	}


	tapGroovePattern = (int)clamp(params[GROOVE_TYPE_PARAM].getValue() + (inputs[GROOVE_TYPE_CV_INPUT].isConnected() ?  inputs[GROOVE_TYPE_CV_INPUT].getVoltage() / 10.0f : 0.0f),0.0f,15.0);
	grooveAmount = clamp(params[GROOVE_AMOUNT_PARAM].getValue() + (inputs[GROOVE_AMOUNT_CV_INPUT].isConnected() ? inputs[GROOVE_AMOUNT_CV_INPUT].getVoltage() / 10.0f : 0.0f),0.0f,1.0f);

	float divisionf = params[CLOCK_DIV_PARAM].getValue();
	if(inputs[CLOCK_DIVISION_CV_INPUT].isConnected()) {
		divisionf +=(inputs[CLOCK_DIVISION_CV_INPUT].getVoltage() * (DIVISIONS / 10.0));
	}
	divisionf = clamp(divisionf,0.0f,20.0f);
	division = (DIVISIONS-1) - int(divisionf); //TODO: Reverse Division Order

	time += 1.0 / args.sampleRate;
	if(inputs[CLOCK_INPUT].isConnected()) {
		if(clockTrigger.process(inputs[CLOCK_INPUT].getVoltage())) {
			if(secondClockReceived) {
				duration = time;
			}
			time = 0;
			secondClockReceived = true;			
			//secondClockReceived = !secondClockReceived;			
		}
		//lights[CLOCK_LIGHT].value = time > (duration/2.0);
	}
	
	if(inputs[CLOCK_INPUT].isConnected()) {
		baseDelay = duration / divisions[division];
	} else {
		baseDelay = clamp(params[TIME_PARAM].getValue() + inputs[TIME_CV_INPUT].getVoltage(), 0.001f, 10.0f);				
	}

	if (pingPongTrigger.process(params[PING_PONG_PARAM].getValue() + inputs[PING_PONG_INPUT].getVoltage())) {
		pingPong = !pingPong;
	}
	lights[PING_PONG_LIGHT].value = pingPong;

	if (reverseTrigger.process(params[REVERSE_PARAM].getValue() + inputs[REVERSE_INPUT].getVoltage())) {
		reverse = !reverse;
		if(reverse) {
			for(int channel =0;channel <CHANNELS;channel++) {
				reverseHistoryBuffer[channel].clear();
			}
		}
	}
	lights[REVERSE_LIGHT].value = reverse;

	grainNumbers = (int)params[GRAIN_QUANTITY_PARAM].getValue();

	for(int channel = 0;channel < CHANNELS;channel++) {
		// Get input to delay block
		float in = 0.0f;
		if(channel == 0) {
			in = inputs[IN_L_INPUT].getVoltage();
		} else {
			in = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltage() : inputs[IN_L_INPUT].getVoltage();			
		}
		feedbackTap[channel] = (int)clamp(params[FEEDBACK_TAP_L_PARAM+channel].getValue() + (inputs[FEEDBACK_TAP_L_INPUT+channel].isConnected() ? (inputs[FEEDBACK_TAP_L_INPUT+channel].getVoltage() / 10.0f) : 0),0.0f,17.0);
		feedbackSlip[channel] = clamp(params[FEEDBACK_L_SLIP_PARAM+channel].getValue() + (inputs[FEEDBACK_L_SLIP_CV_INPUT+channel].isConnected() ? (inputs[FEEDBACK_L_SLIP_CV_INPUT+channel].getVoltage() / 10.0f) : 0),-0.5f,0.5);
		float feedbackAmount = clamp(params[FEEDBACK_PARAM].getValue() + (inputs[FEEDBACK_INPUT].isConnected() ? (inputs[FEEDBACK_INPUT].getVoltage() / 10.0f) : 0), 0.0f, 1.0f);
		float feedbackInput = lastFeedback[channel];

		float dry = in + feedbackInput * feedbackAmount;

		float dryToUse = dry; //Normally the same as dry unless in reverse mode

		// Push dry sample into reverse history buffer
		reverseHistoryBuffer[channel].push(dry);
		if(reverse) {
			float reverseDry = reverseHistoryBuffer[channel].shift();
			dryToUse = reverseDry;
		}

		// Push dry sample into history buffer
		for(int dualIndex=0;dualIndex<2;dualIndex++) {
			if (!historyBuffer[channel][dualIndex].full(NUM_TAPS-1)) {
				historyBuffer[channel][dualIndex].push(dryToUse);
			}
		}

		float wet = 0.0f; // This is the mix of delays and input that is outputed
		float feedbackValue = 0.0f; // This is the output of a tap that gets sent back to input
		float activeTapCount = 0.0f; // This will be used to normalize output
		for(int tap = 0; tap <= NUM_TAPS;tap++) { 

			// Stacking
			if (tap < NUM_TAPS -1 && stackingTrigger[tap].process(params[TAP_STACKED_PARAM+tap].getValue() + inputs[TAP_STACK_CV_INPUT+tap].getVoltage())) {
				tapStacked[tap] = !tapStacked[tap];
			}

			//float pitch_grain_size = 1.0f; //Can be between 0 and 1
			float pitch_grain_size = params[GRAIN_SIZE_PARAM].getValue(); //Can be between 0 and 1
			float pitch,detune;
			if (tap < NUM_TAPS) {
				pitch = floor(params[TAP_PITCH_SHIFT_PARAM+tap].getValue() + (inputs[TAP_PITCH_SHIFT_CV_INPUT+tap].isConnected() ? (inputs[TAP_PITCH_SHIFT_CV_INPUT+tap].getVoltage()*2.4f) : 0));
				detune = floor(params[TAP_DETUNE_PARAM+tap].getValue() + (inputs[TAP_DETUNE_CV_INPUT+tap].isConnected() ? (inputs[TAP_DETUNE_CV_INPUT+tap].getVoltage()*10.0f) : 0));
				tapPitchShift[tap] = pitch;
				tapDetune[tap] = detune;
			} else {
				pitch = floor(params[FEEDBACK_L_PITCH_SHIFT_PARAM+channel].getValue() + (inputs[FEEDBACK_L_PITCH_SHIFT_CV_INPUT+channel].isConnected() ? (inputs[FEEDBACK_L_PITCH_SHIFT_CV_INPUT+channel].getVoltage()*2.4f) : 0));
				detune = floor(params[FEEDBACK_L_DETUNE_PARAM+channel].getValue() + (inputs[FEEDBACK_L_DETUNE_CV_INPUT+channel].isConnected() ? (inputs[FEEDBACK_L_DETUNE_CV_INPUT+channel].getVoltage()*10.0f) : 0));		
				feedbackPitch[channel] = pitch;
				feedbackDetune[channel] = detune;		
			}
			pitch += detune/100.0f; 

			float delayMod = 0.0f;

			//Normally the delay tap is the same as the tap itself, unless it is stacked, then it is its neighbor;
			int delayTap = tap;
			while(delayTap < NUM_TAPS && tapStacked[delayTap]) {
				delayTap++;			
			}
			//Pull feedback off of normal tap time
			if(tap == NUM_TAPS && feedbackTap[channel] < NUM_TAPS) {
				delayTap = feedbackTap[channel];
				delayMod = feedbackSlip[channel] * baseDelay;
			}

			// Compute delay from base and groove
			float delay = baseDelay * lerp(tapGroovePatterns[0][delayTap],tapGroovePatterns[tapGroovePattern][delayTap],grooveAmount); //Balance between straight time and groove

			//External feedback time
			if(tap == NUM_TAPS && feedbackTap[channel] == NUM_TAPS+1) {
				delay = clamp(inputs[EXTERNAL_DELAY_TIME_INPUT].getVoltage(), 0.001f, 10.0f);
			}


			if(inputs[TIME_CV_INPUT].isConnected()) { //The CV can change either clocked or set delay by 10MS
				delayMod = (0.001f * inputs[TIME_CV_INPUT].getVoltage()); 
			}

			delayTime[tap][channel] = (delay + delayMod); 


			//Set reverse size
			if(tap == NUM_TAPS) {
				reverseHistoryBuffer[channel].setDelaySize((delay+delayMod) * args.sampleRate);
			}
		
			const float useWindowingThreshold = .001; //Number of seconds to just change delay time instead of windowing it 
			for(int dualIndex=0;dualIndex<2;dualIndex++) {
				if(abs(actualDelayTime[tap][channel][dualIndex] - delayTime[tap][channel]) < useWindowingThreshold || actualDelayTime[tap][channel][dualIndex] == 0.0f) {
					actualDelayTime[tap][channel][dualIndex] = delayTime[tap][channel];
				} else if (sinOsc[dualIndex].progress() >= 1) {
					actualDelayTime[tap][channel][dualIndex] = delayTime[tap][channel];
					//sinOsc[dualIndex].reset();
				}

				float index = actualDelayTime[tap][channel][dualIndex] * args.sampleRate;

				if(index > 0)
				{
					// How many samples do we need consume to catch up?
					float consume = index - historyBuffer[channel][dualIndex].size(tap);		

					if (outBuffer[tap][channel][dualIndex].empty()) {
						
						double ratio = 1.f;
						if (std::fabs(consume) >= 16.f) {
							ratio = std::pow(10.f, clamp(consume / 10000.f, -1.f, 1.f));
						}

						SRC_DATA srcData;
						srcData.data_in = (const float*) historyBuffer[channel][dualIndex].startData(tap);
						srcData.data_out = (float*) outBuffer[tap][channel][dualIndex].endData();
						srcData.input_frames = std::min((int) historyBuffer[channel][dualIndex].size(tap), 16);
						srcData.output_frames = outBuffer[tap][channel][dualIndex].capacity();
						srcData.end_of_input = false;
						srcData.src_ratio = ratio;
						src_process(src, &srcData);
						historyBuffer[channel][dualIndex].startIncr(tap,srcData.input_frames_used);
						outBuffer[tap][channel][dualIndex].endIncr(srcData.output_frames_gen);
					}
				}

				if (!outBuffer[tap][channel][dualIndex].empty()) {
					initialWindowedOutput[tap][channel][dualIndex] = outBuffer[tap][channel][dualIndex].shift();
				}
			}
			
			
			float wetTap = 0.0f;	
			float sinValue = sinOsc[0].sin();
			float cosValue = sinOsc[1].sin();
			
			float initialOutput = (sinValue * sinValue * initialWindowedOutput[tap][channel][0]) + (cosValue * cosValue * initialWindowedOutput[tap][channel][1]);

			float grainVolumeScaling = 1;
			for(int k=0;k<MAX_GRAINS;k++) {
        	//for(int k=0;k<1;k++) {
        		pitchShiftOut_.l = initialOutput;
				//Apply Pitch Shifting
			    pitch_shifter_[tap][channel][k].set_ratio(SemitonesToRatio(pitch));
			    pitch_shifter_[tap][channel][k].set_size(pitch_grain_size);

			    bool useTriangleWindow = grainNumbers != 4;
			    pitch_shifter_[tap][channel][k].Process(&pitchShiftOut_,useTriangleWindow); 
			    if(k == 0) {
			    	wetTap +=pitchShiftOut_.l; //First one always use
			    } else if (k == 2 && grainNumbers >= 2) {
			    	wetTap +=pitchShiftOut_.l; //Use middle grain for 2
			    	grainVolumeScaling = 1.414;
			    } else if (k != 2 && grainNumbers == 3) {
			    	wetTap +=pitchShiftOut_.l; //Use them all
			    	grainVolumeScaling = 2;
			    }
			}
    		wetTap = wetTap / grainVolumeScaling;		        	
	        	 

			//Feedback tap doesn't get panned or filtered
        	if(tap < NUM_TAPS) {

				// Muting
				if (mutingTrigger[tap].process(params[TAP_MUTE_PARAM+tap].getValue() + (inputs[TAP_MUTE_CV_INPUT+tap].isConnected() ? inputs[TAP_MUTE_CV_INPUT+tap].getVoltage() : 0))) {
					tapMuted[tap] = !tapMuted[tap];
					if(!tapMuted[tap]) {
						activeTapCount +=1.0f;
					}
				}

				float pan = 0.0f;
				if(channel == 0) {
					pan = clamp(1.0-(params[TAP_PAN_PARAM+tap].getValue() + (inputs[TAP_PAN_CV_INPUT+tap].isConnected() ? (inputs[TAP_PAN_CV_INPUT+tap].getVoltage() / 10.0f) : 0)),0.0f,0.5f) * 2.0f;
				} else {
					pan = clamp(params[TAP_PAN_PARAM+tap].getValue() + (inputs[TAP_PAN_CV_INPUT+tap].isConnected() ? (inputs[TAP_PAN_CV_INPUT+tap].getVoltage() / 10.0f) : 0),0.0f,0.5f) * 2.0f;
				}		
				wetTap = wetTap * clamp(params[TAP_MIX_PARAM+tap].getValue() + (inputs[TAP_MIX_CV_INPUT+tap].isConnected() ? (inputs[TAP_MIX_CV_INPUT+tap].getVoltage() / 10.0f) : 0),0.0f,1.0f) * pan;
			

				int tapFilterType = (int)params[TAP_FILTER_TYPE_PARAM+tap].getValue();
				// Apply Filter to tap wet output			
				if(tapFilterType != FILTER_NONE) {
					if(tapFilterType != lastFilterType[tap]) {
						switch(tapFilterType) {
							case FILTER_LOWPASS:
							filterParams[tap].setMode(StateVariableFilterParams<T>::Mode::LowPass);
							break;
							case FILTER_HIGHPASS:
							filterParams[tap].setMode(StateVariableFilterParams<T>::Mode::HiPass);
							break;
							case FILTER_BANDPASS:
							filterParams[tap].setMode(StateVariableFilterParams<T>::Mode::BandPass);
							break;
							case FILTER_NOTCH:
							filterParams[tap].setMode(StateVariableFilterParams<T>::Mode::Notch);
							break;
						}					
					}

					float cutoffExp = clamp(params[TAP_FC_PARAM+tap].getValue() + inputs[TAP_FC_CV_INPUT+tap].getVoltage() / 10.0f,0.0f,1.0f); 
					float tapFc = minCutoff * powf(maxCutoff / minCutoff, cutoffExp) / args.sampleRate;
					if(lastTapFc[tap] != tapFc) {
						filterParams[tap].setFreq(T(tapFc));
						lastTapFc[tap] = tapFc;
					}
					float tapQ = clamp(params[TAP_Q_PARAM+tap].getValue() + (inputs[TAP_Q_CV_INPUT+tap].getVoltage() / 10.0f),0.01f,1.0f) * 50; 
					if(lastTapQ[tap] != tapQ) {
						filterParams[tap].setQ(tapQ); 
						lastTapQ[tap] = tapQ;
					}
					wetTap = StateVariableFilter<T>::run(wetTap, filterStates[tap][channel], filterParams[tap]);
				}
				lastFilterType[tap] = tapFilterType;

				if(tapMuted[tap]) {
					wetTap = 0.0f;
				}

				wet += wetTap;

				lights[TAP_STACKED_LIGHT+tap].value = tapStacked[tap];
				lights[TAP_MUTED_LIGHT+tap].value = (tapMuted[tap]);	

			} else {
				feedbackValue = wetTap;
			}
		}
		
		//activeTapCount = 16.0f;
		//wet = wet / activeTapCount * sqrt(activeTapCount);	

		if(feedbackTap[channel] == NUM_TAPS) { //This would be the All  Taps setting
			//float feedbackScaling = 4.0f; // Trying to make full feedback not, well feedback
			//feedbackValue = wet * feedbackScaling / NUM_TAPS; 
			feedbackValue = wet; 
		}

			
		//Apply global filtering
		// TODO Make it sound better
		float color = clamp(params[FEEDBACK_TONE_PARAM].getValue() + inputs[FEEDBACK_TONE_INPUT].getVoltage() / 10.0f, 0.0f, 1.0f);
		float lowpassFreq = 10000.0f * powf(10.0f, clamp(2.0f*color, 0.0f, 1.0f));
		lowpassFilter[channel].setCutoff(lowpassFreq / args.sampleRate);
		lowpassFilter[channel].process(feedbackValue);
		feedbackValue = lowpassFilter[channel].lowpass();
		float highpassFreq = 10.0f * powf(100.0f, clamp(2.0f*color - 1.0f, 0.0f, 1.0f));
		highpassFilter[channel].setCutoff(highpassFreq / args.sampleRate);
		highpassFilter[channel].process(feedbackValue);
		feedbackValue = highpassFilter[channel].highpass();


		outputs[FEEDBACK_L_OUTPUT+channel].setVoltage(feedbackValue);

		if(inputs[FEEDBACK_L_RETURN+channel].isConnected()) {
			feedbackValue = inputs[FEEDBACK_L_RETURN+channel].getVoltage();
		}

		//feedbackValue = clamp(feedbackValue,-5.0f,5.0f); // Let's keep things civil


		int feedbackDestinationChannel = channel;
		if (pingPong) {
			feedbackDestinationChannel = 1 - channel;
		}
		lastFeedback[feedbackDestinationChannel] = feedbackValue;

		float mix = clamp(params[MIX_PARAM].getValue() + inputs[MIX_INPUT].getVoltage() / 10.0f, 0.0f, 1.0f);
		float out = crossfade(in, wet, mix);  // Not sure this should be wet
		
		outputs[OUT_L_OUTPUT + channel].setVoltage(out);
	}

	//Reset LFOs if needed
	for(int dualIndex = 0; dualIndex < 2;dualIndex++) {
		if (sinOsc[dualIndex].progress() >= 1) {
			sinOsc[dualIndex].reset();
		}
	}
}

struct PWStatusDisplay : TransparentWidget {
	PortlandWeather *module;
	int frame = 0;
	std::shared_ptr<Font> fontNumbers,fontText;

	

	PWStatusDisplay() {
		fontNumbers = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/01 Digit.ttf"));
		fontText = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/DejaVuSansMono.ttf"));
	}

	void drawProgress(const DrawArgs &args, float phase) 
	{
		const float rotate90 = (M_PI) / 2.0;
		float startArc = 0 - rotate90;
		float endArc = (phase * M_PI * 2) - rotate90;

		// Draw indicator
		nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0x20, 0xff));
		{
			nvgBeginPath(args.vg);
			nvgArc(args.vg,75.8,170,35,startArc,endArc,NVG_CW);
			nvgLineTo(args.vg,75.8,170);
			nvgClosePath(args.vg);
		}
		nvgFill(args.vg);
	}

	void drawDivision(const DrawArgs &args, Vec pos, int division) {
		nvgFontSize(args.vg, 28);
		nvgFontFaceId(args.vg, fontNumbers->handle);
		nvgTextLetterSpacing(args.vg, -2);

		nvgFillColor(args.vg, nvgRGBA(0x00, 0xff, 0x00, 0xff));
		char text[128];
		snprintf(text, sizeof(text), "%s", module->divisionNames[division]);
		nvgText(args.vg, pos.x, pos.y, text, NULL);
	}

	void drawDelayTime(const DrawArgs &args, Vec pos, float delayTime) {
		nvgFontSize(args.vg, 28);
		nvgFontFaceId(args.vg, fontNumbers->handle);
		nvgTextLetterSpacing(args.vg, -2);

		nvgFillColor(args.vg, nvgRGBA(0x00, 0xff, 0x00, 0xff));
		char text[128];
		snprintf(text, sizeof(text), "%6.0f", delayTime*1000);	
		nvgText(args.vg, pos.x, pos.y, text, NULL);
	}

	void drawGrooveType(const DrawArgs &args, Vec pos, int grooveType) {
		nvgFontSize(args.vg, 14);
		nvgFontFaceId(args.vg, fontText->handle);
		nvgTextLetterSpacing(args.vg, -2);

		nvgFillColor(args.vg, nvgRGBA(0x00, 0xff, 0x00, 0xff));
		char text[128];
		snprintf(text, sizeof(text), "%s", module->grooveNames[grooveType]);
		nvgText(args.vg, pos.x, pos.y, text, NULL);
	}

	void drawFeedbackTaps(const DrawArgs &args, Vec pos, int *feedbackTaps) {
		nvgFontSize(args.vg, 12);
		nvgFontFaceId(args.vg, fontNumbers->handle);
		nvgTextLetterSpacing(args.vg, -2);

		nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
		for(int i=0;i<CHANNELS;i++) {
			char text[128];
			snprintf(text, sizeof(text), "%s", module->tapNames[feedbackTaps[i]]);
			nvgText(args.vg, pos.x + i*136, pos.y, text, NULL);
		}
	}

	void drawFeedbackPitch(const DrawArgs &args, Vec pos, float *feedbackPitch) {
		nvgFontSize(args.vg, 12);
		nvgFontFaceId(args.vg, fontNumbers->handle);
		nvgTextLetterSpacing(args.vg, -2);

		nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
		for(int i=0;i<CHANNELS;i++) {
			char text[128];
			snprintf(text, sizeof(text), "%-2.0f", feedbackPitch[i]);
			nvgText(args.vg, pos.x + i*136, pos.y, text, NULL);
		}
	}

	void drawFeedbackDetune(const DrawArgs &args, Vec pos, float *feedbackDetune) {
		nvgFontSize(args.vg, 12);
		nvgFontFaceId(args.vg, fontNumbers->handle);
		nvgTextLetterSpacing(args.vg, -2);

		nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
		for(int i=0;i<CHANNELS;i++) {
			char text[128];
			snprintf(text, sizeof(text), "%-3.0f", feedbackDetune[i]);
			nvgText(args.vg, pos.x + i*136, pos.y, text, NULL);
		}
	}


	void drawFilterTypes(const DrawArgs &args, Vec pos, int *filterType) {
		nvgFontSize(args.vg, 14);
		nvgFontFaceId(args.vg, fontText->handle);
		nvgTextLetterSpacing(args.vg, -2);

		nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
		for(int i=0;i<NUM_TAPS;i++) {
			char text[128];
			snprintf(text, sizeof(text), "%s", module->filterNames[filterType[i]]);
			nvgText(args.vg, pos.x + i*63, pos.y, text, NULL);
		}
	}

	void drawTapPitchShift(const DrawArgs &args, Vec pos, float *pitchShift) {
		nvgFontSize(args.vg, 14);
		nvgFontFaceId(args.vg, fontText->handle);
		nvgTextLetterSpacing(args.vg, -2);

		nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
		for(int i=0;i<NUM_TAPS;i++) {
			char text[128];
			snprintf(text, sizeof(text), "%-2.0f", pitchShift[i]);
			nvgText(args.vg, pos.x + i*63, pos.y, text, NULL);
		}
	}

	void drawTapDetune(const DrawArgs &args, Vec pos, float *detune) {
		nvgFontSize(args.vg, 14);
		nvgFontFaceId(args.vg, fontText->handle);
		nvgTextLetterSpacing(args.vg, -2);

		nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
		for(int i=0;i<NUM_TAPS;i++) {
			char text[128];
			snprintf(text, sizeof(text), "%-3.0f", detune[i]);
			nvgText(args.vg, pos.x + i*63, pos.y, text, NULL);
		}
	}

	void drawGrainNumbers(const DrawArgs &args, Vec pos, int grainNumbers) {
		nvgFontSize(args.vg, 12);
		nvgFontFaceId(args.vg, fontNumbers->handle);
		nvgTextLetterSpacing(args.vg, -2);

		nvgFillColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
		char text[128];
		snprintf(text, sizeof(text), "%s", module->grainNames[grainNumbers-1]);
		nvgText(args.vg, pos.x, pos.y, text, NULL);
	}

	void draw(const DrawArgs &args) override {

		if (!module)
			return;
		
		drawDivision(args, Vec(100,65), module->division);
		drawDelayTime(args, Vec(82,125), module->baseDelay);
		drawGrooveType(args, Vec(95,205), module->tapGroovePattern);
		drawFeedbackTaps(args, Vec(363,174), module->feedbackTap);
		drawFeedbackPitch(args, Vec(363,254), module->feedbackPitch);
		drawFeedbackDetune(args, Vec(363,295), module->feedbackDetune);
		drawGrainNumbers(args, Vec(648,77), module->grainNumbers);
		drawFilterTypes(args, Vec(770,190), module->lastFilterType);
		drawTapPitchShift(args, Vec(768,325), module->tapPitchShift);
		drawTapDetune(args, Vec(768,365), module->tapDetune);
	}
};


struct PortlandWeatherWidget : ModuleWidget {
	PortlandWeatherWidget(PortlandWeather *module) {
		setModule(module);

		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/PortlandWeather.svg")));
		
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH - 12, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH + 12, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH - 12, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH + 12, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		{
			PWStatusDisplay *display = new PWStatusDisplay();
			display->module = module;
			display->box.pos = Vec(0, 0);
			display->box.size = Vec(box.size.x, 385);
			addChild(display);
		}

		addParam(createParam<RoundLargeFWSnapKnob>(Vec(12, 40), module, PortlandWeather::CLOCK_DIV_PARAM));
		addParam(createParam<RoundLargeFWSnapKnob>(Vec(12, 100), module, PortlandWeather::TIME_PARAM));
		//addParam(createParam<RoundLargeBlackKnob>(Vec(257, 40), module, PortlandWeather::GRID_PARAM));

		addParam(createParam<RoundLargeFWSnapKnob>(Vec(12, 190), module, PortlandWeather::GROOVE_TYPE_PARAM));
		addParam(createParam<RoundLargeFWKnob>(Vec(200, 190), module, PortlandWeather::GROOVE_AMOUNT_PARAM));

		addParam(createParam<RoundLargeFWKnob>(Vec(280, 55), module, PortlandWeather::FEEDBACK_PARAM));
		addParam(createParam<RoundLargeFWKnob>(Vec(376, 55), module, PortlandWeather::FEEDBACK_TONE_PARAM));

		addParam(createParam<RoundFWSnapKnob>(Vec(293, 153), module, PortlandWeather::FEEDBACK_TAP_L_PARAM));
		addParam(createParam<RoundFWSnapKnob>(Vec(428, 153), module, PortlandWeather::FEEDBACK_TAP_R_PARAM));
		addParam(createParam<RoundFWKnob>(Vec(293, 193), module, PortlandWeather::FEEDBACK_L_SLIP_PARAM));
		addParam(createParam<RoundFWKnob>(Vec(428, 193), module, PortlandWeather::FEEDBACK_R_SLIP_PARAM));
		addParam(createParam<RoundFWSnapKnob>(Vec(293, 233), module, PortlandWeather::FEEDBACK_L_PITCH_SHIFT_PARAM));
		addParam(createParam<RoundFWSnapKnob>(Vec(428, 233), module, PortlandWeather::FEEDBACK_R_PITCH_SHIFT_PARAM));
		addParam(createParam<RoundFWKnob>(Vec(293, 273), module, PortlandWeather::FEEDBACK_L_DETUNE_PARAM));
		addParam(createParam<RoundFWKnob>(Vec(428, 273), module, PortlandWeather::FEEDBACK_R_DETUNE_PARAM));

		addParam(createParam<RoundLargeFWSnapKnob>(Vec(599, 55), module, PortlandWeather::GRAIN_QUANTITY_PARAM));		
		addParam(createParam<RoundLargeFWSnapKnob>(Vec(599, 123), module, PortlandWeather::GRAIN_SIZE_PARAM));
		

		addParam(createParam<CKD6>(Vec(582, 330), module, PortlandWeather::CLEAR_BUFFER_PARAM));


		addParam( createParam<LEDButton>(Vec(335,116), module, PortlandWeather::REVERSE_PARAM));
		addChild(createLight<MediumLight<BlueLight>>(Vec(339, 120), module, PortlandWeather::REVERSE_LIGHT));
		addInput(createInput<PJ301MPort>(Vec(355, 112), module, PortlandWeather::REVERSE_INPUT));

		addParam( createParam<LEDButton>(Vec(400,116), module, PortlandWeather::PING_PONG_PARAM));
		addChild(createLight<MediumLight<BlueLight>>(Vec(404, 120), module, PortlandWeather::PING_PONG_LIGHT));
		addInput(createInput<PJ301MPort>(Vec(420, 112), module, PortlandWeather::PING_PONG_INPUT));



		//last tap isn't stacked
		for (int i = 0; i< NUM_TAPS-1; i++) {
			addParam(createParam<LEDButton>(Vec(690 + 52 + 63*i,37), module, PortlandWeather::TAP_STACKED_PARAM + i));
			addChild(createLight<MediumLight<BlueLight>>(Vec(690 + 56 + 63*i, 41), module, PortlandWeather::TAP_STACKED_LIGHT+i));
			addInput(createInput<PJ301MPort>(Vec(690 + 75+ 63*i, 33), module, PortlandWeather::TAP_STACK_CV_INPUT+i));
		}

		for (int i = 0; i < NUM_TAPS; i++) {
			addParam( createParam<LEDButton>(Vec(690 + 52 + 63*i,63), module, PortlandWeather::TAP_MUTE_PARAM + i));
			addChild(createLight<MediumLight<RedLight>>(Vec(690 + 56 + 63*i, 67), module, PortlandWeather::TAP_MUTED_LIGHT+i));
			addInput(createInput<PJ301MPort>(Vec(690 + 75+ 63*i, 60), module, PortlandWeather::TAP_MUTE_CV_INPUT+i));

			addParam( createParam<RoundFWKnob>(Vec(690 + 48 + 63*i, 90), module, PortlandWeather::TAP_MIX_PARAM + i));
			addInput(createInput<PJ301MPort>(Vec(690 + 81 + 63*i, 91), module, PortlandWeather::TAP_MIX_CV_INPUT+i));
			addParam( createParam<RoundFWKnob>(Vec(690 + 48 + 63*i, 130), module, PortlandWeather::TAP_PAN_PARAM + i));
			addInput(createInput<PJ301MPort>(Vec(690 + 81 + 63*i, 131), module, PortlandWeather::TAP_PAN_CV_INPUT+i));
			addParam( createParam<RoundFWSnapKnob>(Vec(690 + 48 + 63*i, 170), module, PortlandWeather::TAP_FILTER_TYPE_PARAM + i));
			addParam( createParam<RoundFWKnob>(Vec(690 + 48 + 63*i, 210), module, PortlandWeather::TAP_FC_PARAM + i));
			addInput(createInput<PJ301MPort>(Vec(690 + 81 + 63*i, 211), module, PortlandWeather::TAP_FC_CV_INPUT+i));
			addParam( createParam<RoundFWKnob>(Vec(690 + 48 + 63*i, 250), module, PortlandWeather::TAP_Q_PARAM + i));
			addInput(createInput<PJ301MPort>(Vec(690 + 81 + 63*i, 251), module, PortlandWeather::TAP_Q_CV_INPUT+i));
			addParam( createParam<RoundFWSnapKnob>(Vec(690 + 48 + 63*i, 290), module, PortlandWeather::TAP_PITCH_SHIFT_PARAM + i));
			addInput(createInput<PJ301MPort>(Vec(690 + 81 + 63*i, 291), module, PortlandWeather::TAP_PITCH_SHIFT_CV_INPUT+i));
			addParam( createParam<RoundFWKnob>(Vec(690 + 48 + 63*i, 330), module, PortlandWeather::TAP_DETUNE_PARAM + i));
			addInput(createInput<PJ301MPort>(Vec(690 + 81 + 63*i, 331), module, PortlandWeather::TAP_DETUNE_CV_INPUT+i));
		}


		addInput(createInput<PJ301MPort>(Vec(55, 45), module, PortlandWeather::CLOCK_DIVISION_CV_INPUT));
		addInput(createInput<PJ301MPort>(Vec(55, 105), module, PortlandWeather::TIME_CV_INPUT));
		//addInput(createInput<PJ301MPort>(Vec(300, 45), module, PortlandWeather::GRID_CV_INPUT));

		addInput(createInput<PJ301MPort>(Vec(55, 195), module, PortlandWeather::GROOVE_TYPE_CV_INPUT));
		addInput(createInput<PJ301MPort>(Vec(243, 195), module, PortlandWeather::GROOVE_AMOUNT_CV_INPUT));


		addInput(createInput<PJ301MPort>(Vec(323, 60), module, PortlandWeather::FEEDBACK_INPUT));
		addInput(createInput<PJ301MPort>(Vec(419, 60), module, PortlandWeather::FEEDBACK_TONE_INPUT));
		addInput(createInput<PJ301MPort>(Vec(493, 60), module, PortlandWeather::EXTERNAL_DELAY_TIME_INPUT));

		addInput(createInput<PJ301MPort>(Vec(330, 157), module, PortlandWeather::FEEDBACK_TAP_L_INPUT));
		addInput(createInput<PJ301MPort>(Vec(465, 157), module, PortlandWeather::FEEDBACK_TAP_R_INPUT));
		addInput(createInput<PJ301MPort>(Vec(330, 197), module, PortlandWeather::FEEDBACK_L_SLIP_CV_INPUT));
		addInput(createInput<PJ301MPort>(Vec(465, 197), module, PortlandWeather::FEEDBACK_R_SLIP_CV_INPUT));
		addInput(createInput<PJ301MPort>(Vec(330, 237), module, PortlandWeather::FEEDBACK_L_PITCH_SHIFT_CV_INPUT));
		addInput(createInput<PJ301MPort>(Vec(465, 237), module, PortlandWeather::FEEDBACK_R_PITCH_SHIFT_CV_INPUT));
		addInput(createInput<PJ301MPort>(Vec(330, 277), module, PortlandWeather::FEEDBACK_L_DETUNE_CV_INPUT));
		addInput(createInput<PJ301MPort>(Vec(465, 277), module, PortlandWeather::FEEDBACK_R_DETUNE_CV_INPUT));



		addParam(createParam<RoundLargeFWKnob>(Vec(80, 255), module, PortlandWeather::MIX_PARAM));
		addInput(createInput<PJ301MPort>(Vec(125, 260), module, PortlandWeather::MIX_INPUT));


		addInput(createInput<PJ301MPort>(Vec(16, 330), module, PortlandWeather::CLOCK_INPUT));

		addInput(createInput<PJ301MPort>(Vec(75, 330), module, PortlandWeather::IN_L_INPUT));
		addInput(createInput<PJ301MPort>(Vec(105, 330), module, PortlandWeather::IN_R_INPUT));
		addOutput(createOutput<PJ301MPort>(Vec(195, 330), module, PortlandWeather::OUT_L_OUTPUT));
		addOutput(createOutput<PJ301MPort>(Vec(225, 330), module, PortlandWeather::OUT_R_OUTPUT));

		addOutput(createOutput<PJ301MPort>(Vec(311, 330), module, PortlandWeather::FEEDBACK_L_OUTPUT));
		addOutput(createOutput<PJ301MPort>(Vec(341, 330), module, PortlandWeather::FEEDBACK_R_OUTPUT));
		addInput(createInput<PJ301MPort>(Vec(419, 330), module, PortlandWeather::FEEDBACK_L_RETURN));
		addInput(createInput<PJ301MPort>(Vec(449, 330), module, PortlandWeather::FEEDBACK_R_RETURN));

	}

};


Model *modelPortlandWeather = createModel<PortlandWeather, PortlandWeatherWidget>("PortlandWeather");
