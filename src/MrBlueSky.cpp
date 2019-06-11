#include "FrozenWasteland.hpp"
#include "ui/knobs.hpp"
#include "filters/biquad.h"

using namespace std;

#define BANDS 16

struct MrBlueSky : Module {
	enum ParamIds {
		BG_PARAM,
		ATTACK_PARAM = BG_PARAM + BANDS,
		DECAY_PARAM,
		CARRIER_Q_PARAM,
		MOD_Q_PARAM,
		BAND_OFFSET_PARAM,
		GMOD_PARAM,
		GCARR_PARAM,
		G_PARAM,
		SHAPE_PARAM,
		ATTACK_CV_ATTENUVERTER_PARAM,
		DECAY_CV_ATTENUVERTER_PARAM,
		CARRIER_Q_CV_ATTENUVERTER_PARAM,
		MODIFER_Q_CV_ATTENUVERTER_PARAM,
		SHIFT_BAND_OFFSET_CV_ATTENUVERTER_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		CARRIER_IN,
		IN_MOD = CARRIER_IN + BANDS,
		IN_CARR,
		ATTACK_INPUT,
		DECAY_INPUT,
		CARRIER_Q_INPUT,
		MOD_Q_INPUT,
		SHIFT_BAND_OFFSET_LEFT_INPUT,
		SHIFT_BAND_OFFSET_RIGHT_INPUT,
		SHIFT_BAND_OFFSET_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		MOD_OUT,
		OUT = MOD_OUT + BANDS,
		NUM_OUTPUTS
	};
	enum LightIds {
		LEARN_LIGHT,
		NUM_LIGHTS
	};
	Biquad* iFilter[2*BANDS];
	Biquad* cFilter[2*BANDS];
	float mem[BANDS] = {0};
	float freq[BANDS] = {125,185,270,350,430,530,630,780,950,1150,1380,1680,2070,2780,3800,6400};
	float peaks[BANDS] = {0};
	float lastCarrierQ = 0;
	float lastModQ = 0;

	int bandOffset = 0;
	int shiftIndex = 0;
	int lastBandOffset = 0;
	dsp::SchmittTrigger shiftLeftTrigger,shiftRightTrigger;

	MrBlueSky() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		for (int i = 0; i < BANDS; i++) {
			configParam(BG_PARAM + i, 0, 2, 1);
		}
		configParam(ATTACK_PARAM, 0.0, 0.25, 0.0,"Attack");
		configParam(DECAY_PARAM, 0.0, 0.25, 0.0,"Decay");
		configParam(CARRIER_Q_PARAM, 1.0, 15.0, 5.0,"Carrier Q");
		configParam(MOD_Q_PARAM, 1.0, 15.0, 5.0,"Modulator Q");
		configParam(BAND_OFFSET_PARAM, -15.5, 15.5, 0.0,"Band Offset");
		configParam(GMOD_PARAM, 1, 10, 5,"Modulator Gain");
		configParam(GCARR_PARAM, 1, 10, 5,"Carrier Gain");
		configParam(G_PARAM, 1, 10, 5,"Overall Gain");

		configParam(ATTACK_CV_ATTENUVERTER_PARAM, -1.0, 1.0, 0,"Attack CV Attentuation","%",0,100);
		configParam(DECAY_CV_ATTENUVERTER_PARAM, -1.0, 1.0, 0,"Decay CV Attentuation","%",0,100);
		configParam(CARRIER_Q_CV_ATTENUVERTER_PARAM, -1.0, 1.0, 0,"Carrier Q CV Attentuation","%",0,100);
		configParam(MODIFER_Q_CV_ATTENUVERTER_PARAM, -1.0, 1.0, 0,"Modulator Q CV Attentuation","%",0,100);
		configParam(SHIFT_BAND_OFFSET_CV_ATTENUVERTER_PARAM, -1.0, 1.0, 0,"Band Offset CV Attentuation","%",0,100);

		float sampleRate = APP->engine->getSampleRate();

		for(int i=0; i<2*BANDS; i++) {
			iFilter[i] = new Biquad(bq_type_bandpass, freq[i%BANDS] / sampleRate, 5, 6);
			cFilter[i] = new Biquad(bq_type_bandpass, freq[i%BANDS] / sampleRate, 5, 6);
		};
	}

	void process(const ProcessArgs &args) override;

	// void reset() override {
	// 	bandOffset =0;
	// }

};

void MrBlueSky::process(const ProcessArgs &args) {
	// Band Offset Processing
	bandOffset = params[BAND_OFFSET_PARAM].getValue();
	if(inputs[SHIFT_BAND_OFFSET_INPUT].isConnected()) {
		bandOffset += inputs[SHIFT_BAND_OFFSET_INPUT].getVoltage() * params[SHIFT_BAND_OFFSET_CV_ATTENUVERTER_PARAM].getValue();
	}
	if(bandOffset != lastBandOffset) {
		shiftIndex = 0;
		lastBandOffset = bandOffset;
	}

	if(inputs[SHIFT_BAND_OFFSET_LEFT_INPUT].isConnected()) {
		if (shiftLeftTrigger.process(inputs[SHIFT_BAND_OFFSET_LEFT_INPUT].getVoltage())) {
			shiftIndex -= 1;
			if(shiftIndex <= -BANDS) {
				shiftIndex = BANDS -1;
			}
		}
	}

	if(inputs[SHIFT_BAND_OFFSET_RIGHT_INPUT].isConnected()) {
		if (shiftRightTrigger.process(inputs[SHIFT_BAND_OFFSET_RIGHT_INPUT].getVoltage())) {
			shiftIndex += 1;
			if(shiftIndex >= BANDS) {
				shiftIndex = (-BANDS) + 1;
			}
		}
	}

	bandOffset +=shiftIndex;
	//Hack until I can do int clamping
	if(bandOffset <= -BANDS) {
		bandOffset += (BANDS*2) - 1;
	}
	if(bandOffset >= BANDS) {
		bandOffset -= (BANDS*2) + 1;
	}


	//So some vocoding!
	float inM = inputs[IN_MOD].getVoltage()/5;
	float inC = inputs[IN_CARR].getVoltage()/5;
	const float slewMin = 0.001;
	const float slewMax = 500.0;
	const float shapeScale = 1/10.0;
	const float qEpsilon = 0.1;
	float attack = params[ATTACK_PARAM].getValue();
	float decay = params[DECAY_PARAM].getValue();
	if(inputs[ATTACK_INPUT].isConnected()) {
		attack += clamp(inputs[ATTACK_INPUT].getVoltage() * params[ATTACK_CV_ATTENUVERTER_PARAM].getValue() / 20.0f,-0.25f,.25f);
	}
	if(inputs[DECAY_INPUT].isConnected()) {
		decay += clamp(inputs[DECAY_INPUT].getVoltage() * params[DECAY_CV_ATTENUVERTER_PARAM].getValue() / 20.0f,-0.25f,.25f);
	}
	float slewAttack = slewMax * powf(slewMin / slewMax, attack);
	float slewDecay = slewMax * powf(slewMin / slewMax, decay);
	float out = 0.0;

	//Check Mod Q
	float currentQ = params[MOD_Q_PARAM].getValue();
	if(inputs[MOD_Q_PARAM].isConnected()) {
		currentQ += inputs[MOD_Q_INPUT].getVoltage() * params[MODIFER_Q_CV_ATTENUVERTER_PARAM].getValue();
	}

	currentQ = clamp(currentQ,1.0f,15.0f);
	if (abs(currentQ - lastModQ) >= qEpsilon ) {
		for(int i=0; i<2*BANDS; i++) {
			iFilter[i]->setQ(currentQ);
			}
		lastModQ = currentQ;
	}

	//Check Carrier Q
	currentQ = params[CARRIER_Q_PARAM].getValue();
	if(inputs[CARRIER_Q_INPUT].isConnected()) {
		currentQ += inputs[CARRIER_Q_INPUT].getVoltage() * params[CARRIER_Q_CV_ATTENUVERTER_PARAM].getValue();
	}

	currentQ = clamp(currentQ,1.0f,15.0f);
	if (abs(currentQ - lastCarrierQ) >= qEpsilon ) {
		for(int i=0; i<2*BANDS; i++) {
			cFilter[i]->setQ(currentQ);
			}
		lastCarrierQ = currentQ;
	}



	//First process all the modifier bands
	for(int i=0; i<BANDS; i++) {
		float coeff = mem[i];
		float peak = abs(iFilter[i+BANDS]->process(iFilter[i]->process(inM*params[GMOD_PARAM].getValue())));
		if (peak>coeff) {
			coeff += slewAttack * shapeScale * (peak - coeff) / args.sampleRate;
			if (coeff > peak)
				coeff = peak;
		}
		else if (peak < coeff) {
			coeff -= slewDecay * shapeScale * (coeff - peak) / args.sampleRate;
			if (coeff < peak)
				coeff = peak;
		}
		peaks[i]=peak;
		mem[i]=coeff;
		outputs[MOD_OUT+i].setVoltage(coeff * 5.0);
	}

	//Then process carrier bands. Mod bands are normalled to their matched carrier band unless an insert
	for(int i=0; i<BANDS; i++) {
		float coeff;
		if(inputs[(CARRIER_IN+i+bandOffset) % BANDS].isConnected()) {
			coeff = inputs[CARRIER_IN+i+bandOffset].getVoltage() / 5.0;
		} else {
			coeff = mem[(i+bandOffset) % BANDS];
		}

		float bandOut = cFilter[i+BANDS]->process(cFilter[i]->process(inC*params[GCARR_PARAM].getValue())) * coeff * params[BG_PARAM+i].getValue();
		out += bandOut;
	}
	outputs[OUT].setVoltage(out * 5 * params[G_PARAM].getValue());

}

struct MrBlueSkyBandDisplay : TransparentWidget {
	MrBlueSky *module;
	std::shared_ptr<Font> font;

	MrBlueSkyBandDisplay() {
		font = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/Sudo.ttf"));
	}

	void draw(const DrawArgs &args) override {
		if (!module)
			return;
		nvgFontSize(args.vg, 14);
		nvgFontFaceId(args.vg, font->handle);
		nvgStrokeWidth(args.vg, 2);
		nvgTextLetterSpacing(args.vg, -2);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER);
		//static const int portX0[4] = {20, 63, 106, 149};
		for (int i=0; i<BANDS; i++) {
			char fVal[10];
			snprintf(fVal, sizeof(fVal), "%1i", (int)module->freq[i]);
			nvgFillColor(args.vg,nvgRGBA(255, rescale(clamp(module->peaks[i],0.0f,1.0f),0,1,255,0), rescale(clamp(module->peaks[i],0.0f,1.0f),0,1,255,0), 255));
			nvgText(args.vg, 56 + 33*i, 30, fVal, NULL);
		}
	}
};

struct BandOffsetDisplay : TransparentWidget {
	MrBlueSky *module;
	int frame = 0;
	std::shared_ptr<Font> font;

	BandOffsetDisplay() {
		font = APP->window->loadFont(asset::plugin(pluginInstance, "res/fonts/01 Digit.ttf"));
	}

	void drawDuration(const DrawArgs &args, Vec pos, float bandOffset) {
		nvgFontSize(args.vg, 20);
		nvgFontFaceId(args.vg, font->handle);
		nvgTextLetterSpacing(args.vg, -2);

		nvgFillColor(args.vg, nvgRGBA(0x00, 0xff, 0x00, 0xff));
		char text[128];
		snprintf(text, sizeof(text), " % 2.0f", bandOffset);
		nvgText(args.vg, pos.x + 22, pos.y, text, NULL);
	}

	void draw(const DrawArgs &args) override {
		if (!module)
			return;
		drawDuration(args, Vec(0, box.size.y - 150), module->bandOffset);
	}
};

struct MrBlueSkyWidget : ModuleWidget {
	MrBlueSkyWidget(MrBlueSky *module) {
		setModule(module);

		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/MrBlueSky.svg")));
		

		MrBlueSkyBandDisplay *bandDisplay = new MrBlueSkyBandDisplay();
		bandDisplay->module = module;
		bandDisplay->box.pos = Vec(12, 12);
		bandDisplay->box.size = Vec(700, 70);
		addChild(bandDisplay);

		{
			BandOffsetDisplay *offsetDisplay = new BandOffsetDisplay();
			offsetDisplay->module = module;
			offsetDisplay->box.pos = Vec(435, 200);
			offsetDisplay->box.size = Vec(box.size.x, 150);
			addChild(offsetDisplay);
		}

		for (int i = 0; i < BANDS; i++) {
			addParam( createParam<RoundFWKnob>(Vec(53 + 33*i, 120), module, MrBlueSky::BG_PARAM + i));
		}
		addParam(createParam<RoundFWKnob>(Vec(34, 177), module, MrBlueSky::ATTACK_PARAM));
		addParam(createParam<RoundFWKnob>(Vec(116, 177), module, MrBlueSky::DECAY_PARAM));
		addParam(createParam<RoundFWKnob>(Vec(198, 177), module, MrBlueSky::CARRIER_Q_PARAM));
		addParam(createParam<RoundFWKnob>(Vec(280, 177), module, MrBlueSky::MOD_Q_PARAM));
		addParam(createParam<RoundFWSnapKnob>(Vec(392, 177), module, MrBlueSky::BAND_OFFSET_PARAM));
		addParam(createParam<RoundFWKnob>(Vec(40, 284), module, MrBlueSky::GMOD_PARAM));
		addParam(createParam<RoundFWKnob>(Vec(120, 284), module, MrBlueSky::GCARR_PARAM));
		addParam(createParam<RoundFWKnob>(Vec(207, 284), module, MrBlueSky::G_PARAM));

		addParam(createParam<RoundSmallFWKnob>(Vec(37, 238), module, MrBlueSky::ATTACK_CV_ATTENUVERTER_PARAM));
		addParam(createParam<RoundSmallFWKnob>(Vec(119, 238), module, MrBlueSky::DECAY_CV_ATTENUVERTER_PARAM));
		addParam(createParam<RoundSmallFWKnob>(Vec(202, 238), module, MrBlueSky::CARRIER_Q_CV_ATTENUVERTER_PARAM));
		addParam(createParam<RoundSmallFWKnob>(Vec(284, 238), module, MrBlueSky::MODIFER_Q_CV_ATTENUVERTER_PARAM));
		addParam(createParam<RoundSmallFWKnob>(Vec(395, 238), module, MrBlueSky::SHIFT_BAND_OFFSET_CV_ATTENUVERTER_PARAM));


		for (int i = 0; i < BANDS; i++) {
			addInput(createInput<PJ301MPort>(Vec(56 + 33*i, 85), module, MrBlueSky::CARRIER_IN + i));
		}
		addInput(createInput<PJ301MPort>(Vec(42, 330), module, MrBlueSky::IN_MOD));
		addInput(createInput<PJ301MPort>(Vec(122, 330), module, MrBlueSky::IN_CARR));
		addInput(createInput<PJ301MPort>(Vec(36, 209), module, MrBlueSky::ATTACK_INPUT));
		addInput(createInput<PJ301MPort>(Vec(118, 209), module, MrBlueSky::DECAY_INPUT));
		addInput(createInput<PJ301MPort>(Vec(201, 209), module, MrBlueSky::CARRIER_Q_INPUT));
		addInput(createInput<PJ301MPort>(Vec(283, 209), module, MrBlueSky::MOD_Q_INPUT));
		addInput(createInput<PJ301MPort>(Vec(362, 184), module, MrBlueSky::SHIFT_BAND_OFFSET_LEFT_INPUT));
		addInput(createInput<PJ301MPort>(Vec(425, 184), module, MrBlueSky::SHIFT_BAND_OFFSET_RIGHT_INPUT));
		addInput(createInput<PJ301MPort>(Vec(394, 209), module, MrBlueSky::SHIFT_BAND_OFFSET_INPUT));

		for (int i = 0; i < BANDS; i++) {
			addOutput(createOutput<PJ301MPort>(Vec(56 + 33*i, 45), module, MrBlueSky::MOD_OUT + i));
		}
		addOutput(createOutput<PJ301MPort>(Vec(210, 330), module, MrBlueSky::OUT));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH - 12, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH + 12, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH - 12, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH + 12, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
	}
};

Model *modelMrBlueSky = createModel<MrBlueSky, MrBlueSkyWidget>("MrBlueSky");
