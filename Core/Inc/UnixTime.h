/*
*	UnixTime.h, Copyright Jonathan Mackey 2021
*
*	Utility class for converting to/from Unix time.
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
#ifndef UnixTime_h
#define UnixTime_h

#include <inttypes.h>

/*
*	Rather than use time_t, which can be 32 or 64 bit depending on the target,
*	an explicite 32 bit type is used instead.
*/
typedef uint32_t time32_t;

// Note that STM32_CUBE_ is NOT a standard preprocessor macro.  It needs to be
// defined in the STM32 project properties for both targets.
#if !defined __MACH__ && !defined STM32_CUBE_
#define SUPPORT_DSDateTime	1
#endif
#ifdef SUPPORT_DSDateTime
union DSDateTime;
class DS3231SN;
#endif

class UnixTime
{
public:
	static time32_t			StringToUnixTime(
								const char*				inDateStr,
								const char*				inTimeStr);
	static time32_t			StringToUnixTime(
								const char*				inDateTimeStr,
								bool					inAdjustForTimezone=false);
	static void				TimeComponents(
								time32_t				inTime,
								uint8_t&				outHour,
								uint8_t&				outMinute,
								uint8_t&				outSecond);
	static time32_t			DateComponents(
								time32_t				inTime,
								uint16_t&				outYear,
								uint8_t&				outMonth,
								uint8_t&				outDay);
	static bool				CreateTimeStr(
								time32_t				inTime,
								char*					outTimeStr);
	static void				CreateDateStr(
								time32_t				inTime,
								char*					outDateStr);
	static inline uint8_t	DayOfWeek(
								time32_t				inTime)
								{return(((inTime/kOneDay)+4)%7);}
	static void				CreateDayOfWeekStr(
								time32_t				inTime,
								char*					outDayStr);
	static void				CreateMonthStr(
								uint8_t					inMonth,
								char*					outMonthStr);
	static uint8_t			DaysInMonthForYear(
								uint8_t					inMonth,
								uint16_t				inYear);
#ifdef SUPPORT_DSDateTime
	static time32_t			DSDateTimeToUnixTime(
								const DSDateTime&		inDSDateTime);
	static void				UnixTimeToDSDateTime(
								time32_t				inTime,
								DSDateTime&				outDSDateTime);
#endif
	static inline bool		Format24Hour(void)
								{return(sFormat24Hour);}
	static inline void		SetFormat24Hour(
								bool					inFormat24Hour)
								{sFormat24Hour = inFormat24Hour;}
	static void				SDFatDateTime(
								time32_t				inTime,
								uint16_t*				outDate,
								uint16_t*				outTime);
	static uint8_t			StrDecValue(
								const char*				in2ByteStr);
	static void				DecStrValue(
								uint8_t					inDecVal,
								char*					outByteStr);
	static void				Uint16ToDecStr(
								uint16_t				inNum,
								char*					inBuffer);
								
	static void				SetTime(
								const char*				inDateStr,
								const char*				inTimeStr);
	static void				SetTime(
								time32_t				inTime);
	static bool				CreateTimeStr(
								char*					outTimeStr)
								{return(CreateTimeStr(sTime, outTimeStr));}
	static void				CreateDateStr(
								char*					outDateStr)
								{return(CreateDateStr(sTime, outDateStr));}
	static inline void		Tick(void)
								{
									sTime++;
									sTimeChanged = true;
								}
	static inline time32_t	Time(void)
								{return(sTime);}
	static inline time32_t	Date(void)
								{return(sTime - (sTime%kOneDay));}
	static inline bool		TimeChanged(void)
								{return(sTimeChanged);}
	static inline void		ResetTimeChanged(void)
								{sTimeChanged = false;}
	static void				SetUnixTimeFromSerial(void);													
	static void				ResetSleepTime(void);
	static uint32_t			SleepDelay(void)
								{return(sSleepDelay);}
	static inline bool		TimeToSleep(void)
								{return(sSleepTime < Time());}
	static void				SetSleepDelay(
								uint32_t				inDelaySeconds);
	static void				SDFatDateTimeCB(
								uint16_t*				outDate,
								uint16_t*				outTime);
	static void				SetTimeFromExternalRTC(void);
	struct SComponents
	{
		uint8_t		second;
		uint8_t		minute;
		uint8_t		hour;
		uint8_t		day;
		uint8_t		month;
		uint16_t	year;
	};
	static void				ToComponents(
								time32_t				inTime,
								SComponents&			outComponents);
	static time32_t			FromComponents(
								const SComponents&		inComponents);
protected:
#ifdef SUPPORT_DSDateTime
	static DS3231SN*		sExternalRTC;	// This is set via a subclass of UnixTime
#endif
	static time32_t			sSleepTime;
	static time32_t			sTime;
	static uint32_t			sSleepDelay;
	static bool				sTimeChanged;

	static bool				sFormat24Hour;	// false = 12, true = 24
	static const uint8_t	kOneMinute;
	static const uint16_t	kOneHour;
	static const uint32_t	kOneDay;
	static const uint32_t	kOneYear;
	static const time32_t	kYear2000;
	static const uint8_t	kDaysInMonth[];
	static const uint16_t	kDaysTo[];
	static const uint16_t	kDaysToLY[];
	static const char		kMonth3LetterAbbr[];
	static const char		kDay3LetterAbbr[];
};

#endif // UnixTime_h
