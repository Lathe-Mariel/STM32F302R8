/*
 * Hall.c
 *
 *  Created on: Apr 22, 2023
 *      Author: r720r
 */


#include <stdint.h>
#include "main.h"
#include "GlogalVariables.h"
#include "SixsStep.h"
#include "SignalReadWrite.h"
#include "GeneralFunctions.h"
#include "ControlFunctions.h"

// Static Variables
int8_t sOutputMode[3];
uint8_t sVoltageMode;
uint8_t sVoltageMode_pre;
uint8_t sVoltageModeChangedFlg;
uint8_t sVoltageModeModify;
int8_t sRotDir = 0;
uint8_t sLeadAngleModeFlg;
uint8_t sLeadAngleModeFlg_pre;


float sElectAngleActual;
float sElectAngleActual_pre;
float sElectAngleEstimate = 0.0f;
float sIntegral_ElectAngleErr_Ki = 0.0f;
float sElectAngVeloEstimate;
float sElectAngleErr;

// Static Functons
static uint8_t calcVoltageMode(uint8_t* Hall);
static void calcRotDirFromVoltageMode(uint8_t voltageMode_pre, uint8_t voltageMode, int8_t* rotDir);
static float calcElectAngleFromVoltageMode(uint8_t voltageMode, int8_t rotDir);
static uint8_t calcLeadAngleModeFlg(void);
static uint8_t calcVoltageModeFromElectAngle(float electAngle);
static void calcOutputMode(uint8_t voltageMode, int8_t* outputMode);
static void calcDuty(int8_t* outputMode, float DutyRef, float* Duty);


// input DutyRef minus1-1, output Duty 0-1
void sixStepTasks(float DutyRef, float leadAngle, float* Theta, float* Duty, float* outputMode){

	float electAnglePrusLeadAngle;
	float wc_PLL;
	float Kp_PLL;
	float Ki_PLL;
	float Ts_PLL;
	float timeInterval;

	// Read Hall Signals
	readHallSignal(gHall);

	// Hold & Read Input Capture Count
	gInputCaptureCnt_pre = gInputCaptureCnt;
	gInputCaptureCnt = readInputCaptureCnt();

	// Calculate Electrical Freq From Input Capture Count
	if(gInputCaptureCnt != gInputCaptureCnt_pre){
		timeInterval = readTimeInterval(gInputCaptureCnt, gInputCaptureCnt_pre);
		gElectFreq = gfDivideAvoidZero(1.0f, timeInterval, SYSTEMCLOCKCYCLE);
	}

	// Calculate PLL Gain based on Electrical Frequency
	wc_PLL = gElectFreq * 0.5f * TWOPI;
	Ts_PLL = 1.0f / (gElectFreq * 6.0f);
	Kp_PLL = wc_PLL;
	Ki_PLL = 0.2f * wc_PLL * wc_PLL * Ts_PLL;


	// Hold & Calculate Voltage Mode Based on Hall Signals
	sVoltageMode_pre = sVoltageMode;
	sVoltageMode = calcVoltageMode(gHall);

	// Hold & Read Actual Electrical Angle Based on Voltage Mode (Consider with Rotational Direction)
	sElectAngleActual_pre = sElectAngleActual;
	calcRotDirFromVoltageMode(sVoltageMode_pre, sVoltageMode, &sRotDir);
	sElectAngleActual = calcElectAngleFromVoltageMode(sVoltageMode, sRotDir);
	sElectAngleActual = gfWrapTheta(sElectAngleActual);

	// Hold & Calculate Whether to Use Lead Angle Control Mode.
	sLeadAngleModeFlg_pre = sLeadAngleModeFlg;
	sLeadAngleModeFlg = 0;//calcLeadAngleModeFlg();

	if(sLeadAngleModeFlg == 1){
		// Six Step Control using Electrical Angle Consider with Lead Angle
		// Reset EstOmega
		if ( sLeadAngleModeFlg_pre == 0 ){
			sElectAngVeloEstimate = gElectFreq * TWOPI;
			sIntegral_ElectAngleErr_Ki = sElectAngVeloEstimate;
			sElectAngleEstimate = sElectAngleActual;
		}

		// Estimate Electrical Angle & Velocity using PLL
		sElectAngleEstimate += sElectAngVeloEstimate * CARRIERCYCLE;
		sElectAngleEstimate = gfWrapTheta(sElectAngleEstimate);

		electAnglePrusLeadAngle = sElectAngleEstimate + leadAngle;
		electAnglePrusLeadAngle = gfWrapTheta(electAnglePrusLeadAngle);

		sVoltageModeModify = calcVoltageModeFromElectAngle(electAnglePrusLeadAngle);

		if( sElectAngleActual != sElectAngleActual_pre){
			sElectAngleErr = sElectAngleActual - sElectAngleEstimate;

			// wrap Electrical Angle Err
			sElectAngleErr = gfWrapTheta(sElectAngleErr);

			//PLL
			sElectAngVeloEstimate = cfPhaseLockedLoop(sElectAngleErr, Kp_PLL, Ki_PLL, &sIntegral_ElectAngleErr_Ki);
		}

		calcOutputMode(sVoltageModeModify, sOutputMode);

	}

	else{
		// Control without Electrical Angle ( Use Only Hall Signals )
		calcOutputMode(sVoltageMode, sOutputMode);
	}

	// Output Voltage
	calcDuty(sOutputMode, DutyRef, Duty);

	// Output Static Signals
	outputMode = sOutputMode;
	*Theta = sElectAngleEstimate;


}

/*
void getEstHallInput(voltageMode){
	switch(voltageMode){
	  case 3:
	  case 6:
	  	  BEMF = Iuvw_AD [0];
		break;
	  case 2:
	  case 5:
		  BEMF = Iuvw_AD [1];
		  break;
	  case 1:
	  case 4:
		  BEMF = Iuvw_AD [2];
		break;
	  default :
		  BEMF = 0;
	  break;
	}
	if ( BEMF > 100)
	HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_SET);
else
	HAL_GPIO_WritePin(LD2_GPIO_Port, LD2_Pin, GPIO_PIN_RESET);

}*/

static uint8_t calcVoltageMode(uint8_t* Hall){
	uint8_t hallInput;
	uint8_t voltageMode = 0;

	// Convert hall input to digital signal
	hallInput = (Hall[2] << 2) + (Hall[1] << 1) + Hall[0];

	// Decode digital signal to voltage mode
	switch(hallInput){
	  case 3:
		voltageMode = 3;
		break;
	  case 2:
		voltageMode = 4;
		break;
	  case 6:
		voltageMode = 5;
		break;
	  case 4:
		voltageMode = 6;
		break;
	  case 5:
		voltageMode = 1;
		break;
	  case 1:
		voltageMode = 2;
		break;
	  default :
		voltageMode = 0;
	  break;
	}
	return voltageMode;
}
static void calcRotDirFromVoltageMode(uint8_t voltageMode_pre, uint8_t voltageMode, int8_t* rotDir){
	int8_t voltageMode_Diff;

	voltageMode_Diff = (int8_t)voltageMode - (int8_t)voltageMode_pre;

	// Limit voltageMode_Diff minus1-prus1
	if(voltageMode_Diff > 1)
		voltageMode_Diff -= 6;
	else if(voltageMode_Diff < -1)
		voltageMode_Diff += 6;

	// if voltageMode changed, Calculate Rotation direction
	if(voltageMode_Diff != 0)
		*rotDir = voltageMode_Diff;

}

static float calcElectAngleFromVoltageMode(uint8_t voltageMode, int8_t rotDir){
		// Calculate ElectAngle in consideration of rotation direction
		float electAngle;
		float electAngle_Center;

		// Calculate Center ElectAngle of the Area
		switch(voltageMode){
		  case 3:
			  electAngle_Center = 0.0f;
			break;
		  case 4:
			  electAngle_Center = PIDIV3;
			break;
		  case 5:
			  electAngle_Center = PIDIV3 * 2.0f;
			break;
		  case 6:
			  electAngle_Center = PI;
			break;
		  case 1:
			  electAngle_Center = -PIDIV3 * 2.0f;
			break;
		  case 2:
			  electAngle_Center = -PIDIV3;
			break;
		  default :
			  electAngle_Center = 0.0f;
		  break;
		}

		electAngle = electAngle_Center - PIDIV6 * (float)rotDir;

		return electAngle;
}

static uint8_t calcLeadAngleModeFlg(void){
	uint8_t leadAngleModeFlg;

	if(gButton1 == 0)
		leadAngleModeFlg = 1;
	else
		leadAngleModeFlg = 0;

	return leadAngleModeFlg;
}

static uint8_t calcVoltageModeFromElectAngle(float electAngle){
	uint8_t voltageMode = 0;

	//Angle Limit : minus PI - prus PI
	float voltageMode1_StartAngle = -PI + PIDIV6;
	float voltageMode2_StartAngle = -PIDIV3 * 2.0f + PIDIV6;
	float voltageMode3_StartAngle = -PIDIV3 + PIDIV6;
	float voltageMode4_StartAngle = PIDIV6;
	float voltageMode5_StartAngle = PIDIV3 + PIDIV6;
	float voltageMode6_StartAngle = PIDIV3 * 2.0f + PIDIV6;

	if(electAngle >= voltageMode6_StartAngle || electAngle < voltageMode1_StartAngle ){
		voltageMode = 6;
	}
	else if(electAngle >= voltageMode1_StartAngle && electAngle < voltageMode2_StartAngle ){
		voltageMode = 1;
	}
	else if(electAngle >= voltageMode2_StartAngle && electAngle < voltageMode3_StartAngle ){
		voltageMode = 2;
	}
	else if(electAngle >= voltageMode3_StartAngle && electAngle < voltageMode4_StartAngle ){
		voltageMode = 3;
	}
	else if(electAngle >= voltageMode4_StartAngle && electAngle < voltageMode5_StartAngle ){
		voltageMode = 4;
	}
	else if(electAngle >= voltageMode5_StartAngle && electAngle < voltageMode6_StartAngle ){
		voltageMode = 5;
	}
	else
		voltageMode = 0;
	return voltageMode;
}

static void calcOutputMode(uint8_t voltageMode, int8_t* outputMode){
		// Decide output mode
		switch(voltageMode){
		  case 3:
			outputMode[0] = OUTPUTMODE_OPEN;
			outputMode[1] = OUTPUTMODE_POSITIVE;
			outputMode[2] = OUTPUTMODE_NEGATIVE;
			break;
		  case 4:
			outputMode[0] = OUTPUTMODE_NEGATIVE;
			outputMode[1] = OUTPUTMODE_POSITIVE;
			outputMode[2] = OUTPUTMODE_OPEN;
			break;
		  case 5:
			outputMode[0] = OUTPUTMODE_NEGATIVE;
			outputMode[1] = OUTPUTMODE_OPEN;
			outputMode[2] = OUTPUTMODE_POSITIVE;
			break;
		  case 6:
			outputMode[0] = OUTPUTMODE_OPEN;
			outputMode[1] = OUTPUTMODE_NEGATIVE;
			outputMode[2] = OUTPUTMODE_POSITIVE;
			break;
		  case 1:
			outputMode[0] = OUTPUTMODE_POSITIVE;
			outputMode[1] = OUTPUTMODE_NEGATIVE;
			outputMode[2] = OUTPUTMODE_OPEN;
			break;
		  case 2:
			outputMode[0] = OUTPUTMODE_POSITIVE;
			outputMode[1] = OUTPUTMODE_OPEN;
			outputMode[2] = OUTPUTMODE_NEGATIVE;
			break;
		  default :
			outputMode[0] = OUTPUTMODE_OPEN;
			outputMode[1] = OUTPUTMODE_OPEN;
			outputMode[2] = OUTPUTMODE_OPEN;
		  break;
		}
}

static void calcDuty(int8_t* outputMode, float DutyRef, float* Duty){

	// if DutyRef < 0, swap OUTPUTMODE_POSITIVE and OUTPUTMODE_NEGATIVE
	int8_t outputModeMulSwapGain[3];
	int8_t swapGain;

	if (DutyRef > 0)
		swapGain = 1;
	else
		swapGain = -1;


	outputModeMulSwapGain[0]  = outputMode[0] * swapGain;
	outputModeMulSwapGain[1]  = outputMode[1] * swapGain;
	outputModeMulSwapGain[2]  = outputMode[2] * swapGain;

	if( outputModeMulSwapGain[0] < 0) outputModeMulSwapGain[0] = 0;
	if( outputModeMulSwapGain[1] < 0) outputModeMulSwapGain[1] = 0;
	if( outputModeMulSwapGain[2] < 0) outputModeMulSwapGain[2] = 0;


	Duty[0] = (float)(outputModeMulSwapGain[0] * DutyRef * swapGain);
	Duty[1] = (float)(outputModeMulSwapGain[1] * DutyRef * swapGain);
	Duty[2] = (float)(outputModeMulSwapGain[2] * DutyRef * swapGain);

}
