//============ Copyright (c) Valve Corporation, All rights reserved. ============
#include "driverlog.h"

#include <stdarg.h>
#include <stdio.h>

#if !defined( WIN32 )
#define vsnprintf_s vsnprintf
#endif

static void DriverLogVarArgs( const char *pMsgFormat, va_list args )
{
	char buf[ 1024 ];
#if defined( WIN32 )
	int len = sprintf_s( buf, sizeof( buf ), "[rayneo] " );
	if ( len > 0 && (size_t)len < sizeof( buf ) )
	{
		vsnprintf_s( buf + len, sizeof( buf ) - len, _TRUNCATE, pMsgFormat, args );
	}
	else
	{
		vsnprintf_s( buf, sizeof( buf ), _TRUNCATE, pMsgFormat, args );
	}
#else
	int len = snprintf( buf, sizeof( buf ), "[rayneo] " );
	if ( len > 0 && (size_t)len < sizeof( buf ) )
	{
		vsnprintf( buf + len, sizeof( buf ) - len, pMsgFormat, args );
	}
	else
	{
		vsnprintf( buf, sizeof( buf ), pMsgFormat, args );
	}
#endif

	vr::VRDriverLog()->Log( buf );
}


void DriverLog( const char *pMsgFormat, ... )
{
	va_list args;
	va_start( args, pMsgFormat );

	DriverLogVarArgs( pMsgFormat, args );

	va_end( args );
}


void DebugDriverLog( const char *pMsgFormat, ... )
{
#ifdef _DEBUG
	va_list args;
	va_start( args, pMsgFormat );

	DriverLogVarArgs( pMsgFormat, args );

	va_end( args );
#endif
}
