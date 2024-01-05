/*
*	UnixTimeWWVB.h, Copyright Jonathan Mackey 2023
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

#include "UnixTime.h"
#ifdef STM32_CUBE_
#include "stm32f1xx_hal.h"
#endif

// See https://en.wikipedia.org/wiki/WWVB for a description of the fields.
struct SWWVBTimeCode
{
	uint8_t	minutes10[4];		// 00
	uint8_t	z0;					// 04
	uint8_t	minutes1[4];		// 05
	uint8_t	p1;					// 09
	uint8_t	hours10[4];			// 10
	uint8_t	z1;					// 14
	uint8_t	hours1[4];			// 15
	uint8_t	p2;					// 19
	uint8_t	dayOfYear100[4];	// 20
	uint8_t	z2;					// 24
	uint8_t	dayOfYear10[4];		// 25
	uint8_t	p3;					// 29
	uint8_t	dayOfYear1[4];		// 30
	uint8_t	z3;					// 34
	uint8_t	dutSign[4];			// 35
	uint8_t	p4;					// 39
	uint8_t	dutValue[4];		// 40
	uint8_t	z4;					// 44
	uint8_t	year10[4];			// 45
	uint8_t	p5;					// 49
	uint8_t	year1[4];			// 50
	uint8_t	z5;					// 54
	uint8_t	leapYearIndicator;	// 55
	uint8_t	leapSecondAtEOM;	// 56
	uint8_t	dstStatus[2];		// 57
	uint8_t	p0;					// 59
};

class UnixTimeWWVB : public UnixTime
{
public:
#ifdef STM32_CUBE_
	static void				InitWWVB(
								RTC_HandleTypeDef*	inRTCHndl,
								TIM_HandleTypeDef*	inTim2Hndl,
								TIM_HandleTypeDef*	inTim3Hndl,
								UART_HandleTypeDef*	inUART2Hndl);
	static void				WakeUpGPSModule(void);
	static void				PutGPSModuleToSleep(void);
#endif
	/*
	*	UnixTimeFromRMCString is a minimal parser that ONLY extracts the date
	*	and time from a valid NMEA RMC string.  The resulting date and time is
	*	returned as a uint32_t Unix time value.
	*
	*	Per the RMC spec, field [1] is the UTC time of the form hhmmss.sss, and
	*	field [9] is the date of the form ddmmyy.  All other fields except for
	*	the checksum are ignored.
	*
	*	The NMEA string passed may not be an RMC string.  In this case and if
	*	there is a checksum error, zero is returned.
	*/
	static time32_t			UnixTimeFromRMCString(
								const char*				inString);
	static void				LoadTimeCodeStruct(
								time32_t				inTime,
								SWWVBTimeCode&			outTCS);
	enum eDST
	{						// 57	58
		eDST_NotInEffect,	//  0	 0
		eDST_EndsToday,		//	0	 1
		eDST_BeginsToday,	//	1	 0
		eDST_InEffect		//	1	 1
	};
	enum eDUT
	{
		eDUT_Negative = 2,
		eDUT_Positive = 5
	};
	
#ifdef STM32_CUBE_
	static TIM_HandleTypeDef* sTim2Hndl;
	static UART_HandleTypeDef* sUART2Hndl;
#endif
protected:
//	time32_t	sDSTStartTime;	// Month day start time (no year component)
//	time32_t	sDSTEndTime;	// Month day end time (zero if no DST)
	
	static void				ToTimeCode8421(
								uint16_t				inValue,
								uint8_t					out8421_100[4],
								uint8_t					out8421_10[4],
								uint8_t					out8421_1[4]);
	static void				To8421(
								uint8_t					inValue,
								uint8_t					out8421[4]);
};
