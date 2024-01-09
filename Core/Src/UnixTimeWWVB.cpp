/*
*	UnixTimeWWVB.cpp, Copyright Jonathan Mackey 2023
*
*	Support for WWVB simulation.
*
*	This class must be subclassed in order to use any functions that reference
*	sTime and/or something needs to call Tick() once per second.  Tick() is
*	generally called via an ISR specific to the MCU being used.
*
*	GNU license:
*	This program is free software: you can redistribute it and/or modify
*	it under the terms of the GNU General Public License as published by
*	the Free Software Foundation, either version 3 of the License, or
*	(at your option) any later version.
*
*	This program is distributed in the hope that it will be useful,
*	but WITHOUT ANY WARRANTY; without even the implied warranty of
*	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*	GNU General Public License for more details.
*
*	You should have received a copy of the GNU General Public License
*	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*
*	Please maintain this license information along with authorship and copyright
*	notices in any redistribution of this code.
*
*/
#include "UnixTimeWWVB.h"
//#ifdef STM32_CUBE_	// Note this NOT a standard preprocessor macro.

static volatile uint32_t	sDuration;
static volatile uint32_t	sTenthsCount;
static volatile uint32_t	sTimeCodeBitCount;
static volatile uint32_t	sTimeToNextGPSUpdate;
static SWWVBTimeCode		sWWVBTimeCode;
static uint8_t				sByteReceived;
static char					sNMEAStrBuf[128];
static char					sNMEAHexStrBuf[15];
static volatile uint32_t	sNMEAStrIdx;
#define HIGH_OUTPUT		66
#define LOW_OUTPUT		0

#ifdef STM32_CUBE_
TIM_HandleTypeDef* UnixTimeWWVB::sTim2Hndl;
UART_HandleTypeDef* UnixTimeWWVB::sUART2Hndl;

/*
*	Set DEBUG_WWVB_TIMING to 1 to use PB0 and PB1 to debug the WWVB timing
*	using a logic analyzer.  Also transmits the received Unix time received
*	from the GPS module.
*
*	Set DEBUG_WWVB_TIMING to 0 to disable.
*/
#define DEBUG_WWVB_TIMING	1

/********************************** InitWWVB **********************************/
void UnixTimeWWVB::InitWWVB(
	RTC_HandleTypeDef*	inRTCHndl,
	TIM_HandleTypeDef*	inTim2Hndl,
	TIM_HandleTypeDef*	inTim3Hndl,
	UART_HandleTypeDef*	inUART2Hndl)
{
	sTim2Hndl = inTim2Hndl;
	sUART2Hndl = inUART2Hndl;
	
	HAL_RTCEx_SetSecond_IT(inRTCHndl);
	__HAL_RTC_SECOND_CLEAR_FLAG(inRTCHndl, RTC_FLAG_SEC);
	
	/*
	*	TIM3->PSC is the prescaler or divisor of the clock.  In this case
	*	the clock is 8MHz and the PSC is 1.
	*	TIM3->ARR (133) sets the PWM period, i.e. the frequency.
	*	The frequency is therefore (Clock/PSC)/ARR = 8MHz/133 = 60150Hz
	*	TIM3->CCR1 sets the pulse duration within the period set by TIM3->ARR
	*	Therefore CCR1 can be no more than ARR.
	*
	*	All bits start at low output (in this case none) as specified in
	*	the WWVB documentation.
	*/
	TIM3->CCR1 = LOW_OUTPUT;

	sDuration = 2;
	sTenthsCount = 0;
	sNMEAStrIdx = 0;
	UnixTime::SetTime(0x6423FFF0);	// 0x6423FFF0 = 29-MAR-2023 09:08:00
	UnixTimeWWVB::LoadTimeCodeStruct(0x6423FFF0, sWWVBTimeCode);	// initialize with dummy time
	sTimeCodeBitCount = sizeof(SWWVBTimeCode)-1;	// Force a new frame to be generated.

	WakeUpGPSModule();

	/*
	*	Start calling both TIM2 & TIM3 interrupt callbacks.
	*/
	HAL_TIM_Base_Start_IT(sTim2Hndl);
	HAL_TIM_PWM_Start(inTim3Hndl, TIM_CHANNEL_1);
 }

/******************************* UInt32ToHexStr *******************************/
void UInt32ToHexStr(
	uint32_t	inNum,
	char*		inBuffer)
{
	static const char kHexChars[] = "0123456789ABCDEF";
	uint8_t i = 8;
	while (i)
	{
		i--;
		inBuffer[i] = kHexChars[inNum & 0xF];
		inNum >>= 4;
	}
	inBuffer[8] = 0;
}

/****************************** WakeUpGPSModule *******************************/
/*
*	Wakes up the GPS module by applying power via a MOSFET controlled by PB10.
*/
void UnixTimeWWVB::WakeUpGPSModule(void)
{
	/*
	*	Apply power to the GPS module
	*/
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_SET);
	
	/*
	*	Turn off the on-board status LED on pin PB2 to show that the GPS hasn't
	*	acquired a satellite yet (and hasn't set the time.)
	*	Note that GPIO_PIN_2/PB2 is the blue LED on BluePill+ boards.
	*	Standard BluePill boards use PC13.
	*/
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_2, GPIO_PIN_RESET);
	
	sTimeToNextGPSUpdate = 0;
	HAL_UART_Receive_IT(sUART2Hndl, &sByteReceived, 1);
}

/**************************** PutGPSModuleToSleep *****************************/
/*
*	- Puts the GPS module to sleep by disconnecting the module's power via a
*	MOSFET controlled by PB10.
*	- Calculates the next wakup of the GPS to update the time
*/
void UnixTimeWWVB::PutGPSModuleToSleep(void)
{
	/*
	*	Remove power from the GPS module
	*/
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_10, GPIO_PIN_RESET);
	{
		time32_t	time = Time();
		uint8_t	hour, minute, second;
		TimeComponents(time, hour, minute, second);
		// Truncate the time to remove minutes and seconds.
		time -= ((minute * 60) + second);
		// Set the time to the next half hour
		time += (minute >= 30 ? (90*60):(30*60));
		sTimeToNextGPSUpdate = time;
		//	UInt32ToHexStr(time, sNMEAHexStrBuf);
		//	sNMEAHexStrBuf[8] = '\n';
		//	HAL_UART_Transmit_IT(UnixTimeWWVB::sUART2Hndl, (uint8_t*)sNMEAHexStrBuf, 9);
	}
	HAL_UART_AbortReceive(sUART2Hndl);
}
#endif

#define CHECK_RMC_STATUS 0
/**************************** UnixTimeFromRMCString ***************************/
//$GNRMC,192503.00,A,4420.87057,N,07111.35174,W,0.049,,231223,,,A,V*09\r\n
uint32_t UnixTimeWWVB::UnixTimeFromRMCString(
	const char*	inString)
{
	uint32_t	unixTime = 0;
	/*
	*	If the string is not nil AND
	*	its preamble is $GNRMC
	*/
	if (inString &&
		inString[0] == '$' &&
		//inString[3] == 'R' &&
		//inString[4] == 'M' &&
		inString[5] == 'C')
	{
		const char* stringPtr = &inString[1];
		char	thisChar;
		uint32_t	timeValue = 0;
		uint32_t	timeSubfieldIndex = 0; // 01 hour, 23 minute, 45 second
		uint32_t	month = 0;
		uint32_t	dateSubfieldIndex = 0;	// 01 day, 23 month, 45 year
		uint32_t	nmeaFieldIndex = 0;
		// Checksum field starts with an asterisk and consists of 2 hex values.
		// The checksum is the XOR of all characters between $ and *
		uint8_t		crc = 0;
		uint8_t		expectedCRC = 0;
		bool		crcAsteriskHit = false;
#if CHECK_RMC_STATUS
		bool		dataValid = false;
#endif
		while ((thisChar = *(stringPtr++)) != 0)
		{
			switch (thisChar)
			{
				case ',':
					crc ^= (uint8_t)',';
					nmeaFieldIndex++;
					break;
				case '*':
					crcAsteriskHit = true;
					break;
				default:
					if (!crcAsteriskHit)
					{
						crc ^= (uint8_t)thisChar;
						switch(nmeaFieldIndex)
						{
							case 1:	// Time field of the form hhmmss.ss
								if (timeSubfieldIndex < 6)
								{
									if (timeSubfieldIndex & 1)
									{
										timeValue += (thisChar-'0');
										switch(timeSubfieldIndex)
										{
											case 1:	// Add hours
												unixTime += (timeValue*kOneHour);
												break;
											case 3:	// Add minutes
												unixTime += (timeValue*kOneMinute);
												break;
											case 5:	// Add seconds
												unixTime += timeValue;
												break;
										}
										timeValue = 0;
									} else
									{
										timeValue = (thisChar-'0')*10;
									}
									timeSubfieldIndex++;
								} // else fractional second subfield is ignored
								break;
#if CHECK_RMC_STATUS
							case 2:	// Status, V = warning, A = Valid
								dataValid = thisChar == 'A';
								break;
#endif
							case 9:	// Date field of the form ddmmyy
								if (dateSubfieldIndex < 6)
								{
									if (dateSubfieldIndex & 1)
									{
										timeValue += (thisChar-'0');
										switch(dateSubfieldIndex)
										{
											case 1:	// Add days
												unixTime += (timeValue*kOneDay);
												break;
											case 3:	// Add months
												month = timeValue;
												unixTime += (kDaysTo[month-1]*kOneDay);
												break;
											case 5:	// Add years 
												unixTime += (timeValue*31536000);
												/*
												*	If past February AND
												*	year is a leap year THEN
												*	add a day
												*/
												if (month > 2 &&
													(timeValue % 4) == 0)
												{
													unixTime+=kOneDay;
												}
												// Account for leap year days since 2000
												unixTime += ((((timeValue+3)/4)-1)*kOneDay);

												break;
										}
										timeValue = 0;
									} else
									{
										timeValue = (thisChar-'0')*10;
									}
									dateSubfieldIndex++;
								}
								break;
						}
					} else
					{
						thisChar -= '0';
						/*
						*	If thisChar is 'A' to 'F'
						*/
						if (thisChar > 9)
						{
							thisChar -= 7;
							/* If lowercase hex...
							if (thisChar > 15)
							{
								thisChar -= 32;
							}
							*/
						}
						expectedCRC = (expectedCRC << 4) + thisChar;
					}
					break;
			}
		}
#if CHECK_RMC_STATUS
		if (crcAsteriskHit &&
			crc == expectedCRC &&
			dataValid)
#else
		if (crcAsteriskHit &&
			crc == expectedCRC)
#endif
		{
			if (unixTime)
			{
				/*
				*	Sometimes on startup the GPS module, before it aquires a
				*	satellite, returns a garbage GNRMC that only contains the time.
				*
				*	If all of the time and date fields were parsed...
				*/
				if (timeSubfieldIndex > 5 &&
					 dateSubfieldIndex > 5)
				{
					unixTime += kYear2000;	// Seconds from 1970 to 2000
				} else
				{
					unixTime = 0;
				}
			}
		} else
		{
			unixTime = 0;
		}
	}
	return(unixTime);
}

/***************************** LoadTimeCodeStruct *****************************/
/*
*	outTCS is an array of 60 bytes.  Each byte represents the type of bit to be
*	broadcast within a one second interval:
*		0 = 0 bit = 0.2s
*		1 = 1 bit = 0.5s
*		2 = marker = 0.8s
*	
*/
void UnixTimeWWVB::LoadTimeCodeStruct(
	time32_t		inTime,
	SWWVBTimeCode&	outTCS)
{
	uint8_t	hour, minute, second, month, day;
	uint16_t	year;
	TimeComponents(DateComponents(inTime, year, month, day), hour, minute, second);
	bool		isLY = (year%4) == 0;
	uint16_t	dayOfYear = (isLY ? kDaysToLY : kDaysTo)[month-1] + day;
	
	// Date
	ToTimeCode8421(minute, nullptr, outTCS.minutes10, outTCS.minutes1);
	ToTimeCode8421(hour, nullptr, outTCS.hours10, outTCS.hours1);
	ToTimeCode8421(dayOfYear, outTCS.dayOfYear100, outTCS.dayOfYear10, outTCS.dayOfYear1);
	ToTimeCode8421(year%100, nullptr, outTCS.year10, outTCS.year1);
	// DUT, subtract 0.3s to account for latency (0.3s is just a guess)
	To8421(eDUT_Negative, outTCS.dutSign);
	To8421(3, outTCS.dutValue);
	
	/*
	*	Initialize the daylight savings time bits based on the month, day and
	*	day of week.
	*/
	{
		uint8_t	dstStatus = eDST_NotInEffect;	// i.e. Standard time
		switch (month)
		{
			//case 1:
			//case 2:
			//case 11:
			//case 12:
			//	break;
			case 3:
			{
				// DST begins on the 2nd Sunday in March at 2AM
				uint8_t	dow = DayOfWeek(inTime);	// 0 = Sun, 6 = Sat
				// S M T W T F S
				// 0 1 2 3 4 5 6
				uint8_t	elaspsedSundays = (day + 6 - dow)/7;
				/*
				*	If dow is Sunday AND
				*	this is the 2nd Sunday of the month THEN
				*	DST begins today.
				*/
				if (dow == 0 && elaspsedSundays == 2)
				{
					/*
					*	According to the NIST, on the day DST begins, only bit
					*	57 is set (eDST_BeginsToday)
					*/
					dstStatus = eDST_BeginsToday;
				/*
				*	If the 2nd Sunday in March has passed THEN
				*	DST is in effect.
				*/
				} else if (elaspsedSundays >= 2)
				{
					dstStatus = eDST_InEffect;
				}
				break;
			}
			case 4:
			case 5:
			case 6:
			case 7:
			case 8:
			case 9:
			case 10:
				dstStatus = eDST_InEffect;
				break;
			case 11:
			{
				// DST ends on the first Sunday in November at 2AM
				uint8_t	dow = DayOfWeek(inTime);	// 0 = Sun, 6 = Sat
				uint8_t	elaspsedSundays = (day + 6 - dow)/7;
				/*
				*	If dow is Sunday AND
				*	this is the 1st Sunday of the month THEN
				*	DST ends today.
				*/
				if (dow == 0 && elaspsedSundays == 1)
				{
					/*
					*	According to the NIST, on the day DST ends, only bit 58
					*	is set (eDST_EndsToday)
					*/
					dstStatus = eDST_EndsToday;
				/*
				*	If the 1st Sunday in November hasn't passed THEN
				*	DST is in effect.
				*/
				} else if (elaspsedSundays < 1)
				{
					dstStatus = eDST_InEffect;
				}
				break;
			}
			
		}
		outTCS.dstStatus[0] = dstStatus >> 1;
		outTCS.dstStatus[1] = dstStatus & 1;
	}

 	outTCS.leapYearIndicator = isLY;
	outTCS.leapSecondAtEOM = 0;

	// Set the markers
	outTCS.minutes10[0] = outTCS.p1 = outTCS.p2 = outTCS.p3 = outTCS.p4 = outTCS.p5 = outTCS.p0 = 2;
	// Zero out any values that aren't already set to zero as part of one of the
	// 8421 values.
	outTCS.z0 = outTCS.z1 = outTCS.z2 = outTCS.z3 = outTCS.z4 = outTCS.z5 = 0;
}

/*********************************** To8421 ***********************************/
void UnixTimeWWVB::To8421(
	uint8_t		inValue,
	uint8_t		out8421[4])
{
	out8421[3] = inValue & 1;
	out8421[2] = (inValue & 2) >> 1;
	out8421[1] = (inValue & 4) >> 2;
	out8421[0] = (inValue & 8) >> 3;
}

/********************************* ToTimeCode *********************************/
void UnixTimeWWVB::ToTimeCode8421(
	uint16_t	inValue,
	uint8_t		out8421_100[4],
	uint8_t		out8421_10[4],
	uint8_t		out8421_1[4])
{
	if (out8421_100)
	{
		To8421(inValue/100, out8421_100);
		inValue %= 100;
	}
	To8421(inValue/10, out8421_10);
	To8421(inValue %= 10, out8421_1);
}

#ifdef STM32_CUBE_
/************************ HAL_TIM_PeriodElapsedCallback ***********************/
/**
  * @brief  Period elapsed callback in non-blocking mode
  * @param  htim TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
	/* Prevent unused argument(s) compilation warning */
	UNUSED(htim);

	if (sDuration == sTenthsCount)
	{
		TIM3->CCR1 = HIGH_OUTPUT;	// 50% duty (symmetrical square wave)
#if DEBUG_WWVB_TIMING
		HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_SET);
#endif
	}
	sTenthsCount++;
#if DEBUG_WWVB_TIMING
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_RESET);
#endif
}

/************************* HAL_RTCEx_RTCEventCallback *************************/
/**
  * @brief  Second event callback.
  * @param  hrtc: pointer to a RTC_HandleTypeDef structure that contains
  *                the configuration information for RTC.
  * @retval None
  */
void HAL_RTCEx_RTCEventCallback(RTC_HandleTypeDef *hrtc)
{
	/* Prevent unused argument(s) compilation warning */
	UNUSED(hrtc);

	UnixTime::Tick();
		
	if (sTimeCodeBitCount < 59)
	{
		sTimeCodeBitCount++;
	} else
	{
		time32_t	thisTime = UnixTime::Time();
		/*
		*	Because WWVB time code frames don't include seconds, the frame
		*	always starts on an even minute.
		*
		*	If on even minute (no seconds) THEN
		*	Generate a new WWVB time code frame
		*/
		if (thisTime % 60 == 0)
		{
			// Generate a new WWVB time code frame.
			UnixTimeWWVB::LoadTimeCodeStruct(thisTime, sWWVBTimeCode);
			sTimeCodeBitCount = 0;
#if DEBUG_WWVB_TIMING
			HAL_GPIO_WritePin(GPIOB, GPIO_PIN_1, GPIO_PIN_SET);
//			UInt32ToHexStr(thisTime, sNMEAHexStrBuf);
//			sNMEAHexStrBuf[8] = '\n';
//			HAL_UART_Transmit_IT(UnixTimeWWVB::sUART2Hndl, (uint8_t*)sNMEAHexStrBuf, 9);
#endif
		}
		
		/*
		*	If it's time to update the time using the GPS module
		*/
		if (sTimeToNextGPSUpdate &&
			sTimeToNextGPSUpdate <= thisTime)
		{
			UnixTimeWWVB::WakeUpGPSModule();
		}
	}
	static const uint8_t	kBitDurations[] = {2,5,8};// 0.2s, 0.5s, 0.8s = 0, 1, M
	sDuration = kBitDurations[sWWVBTimeCode.minutes10[sTimeCodeBitCount]];
	/*
	*	All bits start at low output (in this case none) as specified in
	*	the WWVB documentation.
	*/
	TIM3->CCR1 = LOW_OUTPUT;
	
#if DEBUG_WWVB_TIMING
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0, GPIO_PIN_RESET);
#endif
	
	// Start a new bit
	sTenthsCount = 0;
	
	// Sync TIM2 to the RTC
	HAL_TIM_GenerateEvent(UnixTimeWWVB::sTim2Hndl, TIM_EGR_UG);
}

/************************** HAL_UART_RxCpltCallback ***************************/
/**
  * @brief  Rx Transfer completed callbacks.
  * @param  huart  Pointer to a UART_HandleTypeDef structure that contains
  *                the configuration information for the specified UART module.
  * @retval None
  */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
	if (sTimeToNextGPSUpdate == 0)
	{
		switch (sByteReceived)
		{
			case '\n':	// Ignore <NL>
				break;
			case '\r':	// Process received NMEA string from GPS module.
			{
				sNMEAStrBuf[sNMEAStrIdx] = 0;	// Terminate string
				sNMEAStrIdx = 0;				// Reset index
				time32_t	timeRxd = UnixTimeWWVB::UnixTimeFromRMCString(sNMEAStrBuf);
				/*
				*	The string received could be any NMEA string, but only RMC
				*	strings containing a valid time and date are processed by
				*	UnixTimeFromRMCString().
				*
				*	If the string was a valid NMEA RMC string THEN
				*	Update/Set the time.
				*/
				if (timeRxd)
				{
					/*
					*	After setting the UnixTime::time the STM32 RTC seconds count
					*	could be updated as well.  There is no reason to use the RTC
					*	for anything other than getting the second tick iterrupt, so
					*	no reason to update the STM32 RTC_CNTH & RTC_CNTL (seconds.)
					*/
					UnixTime::SetTime(timeRxd);
					
					// Turn on status LED to show that the time was successfully
					// updated by the GPS.
					HAL_GPIO_WritePin(GPIOB, GPIO_PIN_2, GPIO_PIN_SET);
					
					UnixTimeWWVB::PutGPSModuleToSleep();
					return;
				}
				break;
			}
			default:
				if (sNMEAStrIdx < (sizeof(sNMEAStrBuf)-2))
				{
					sNMEAStrBuf[sNMEAStrIdx++] = sByteReceived;
				}
				break;
		}
		HAL_UART_Receive_IT(huart, &sByteReceived, 1);
	}
}
#endif
