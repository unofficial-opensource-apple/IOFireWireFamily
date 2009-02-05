/*
 *  IOFWBufferFillIsochPort.cpp
 *  IOFireWireFamily
 *
 *  Created by Niels on Mon Sep 09 2002.
 *  Copyright (c) 2002 Apple Computer, Inc. All rights reserved.
 *
 * $Log: IOFWBufferFillIsochPort.cpp,v $
 * Revision 1.6  2007/03/14 01:01:12  collin
 * *** empty log message ***
 *
 * Revision 1.5  2004/05/04 22:52:19  niels
 * *** empty log message ***
 *
 * Revision 1.4  2004/03/16 02:34:16  collin
 * unit termination code
 *
 * Revision 1.3  2004/01/22 01:49:59  niels
 * fix user space physical address space getPhysicalSegments
 *
 * Revision 1.2  2003/07/21 06:52:58  niels
 * merge isoch to TOT
 *
 * Revision 1.1.2.2  2003/07/18 00:17:41  niels
 * *** empty log message ***
 *
 * Revision 1.1.2.1  2003/07/01 20:54:06  niels
 * isoch merge
 *
 */

#import "IOFWBufferFillIsochPort.h"

#undef super
#define super IOFWIsochPort

OSDefineMetaClassAndAbstractStructors( IOFWBufferFillIsochPort, IOFWIsochPort )

bool
IOFWBufferFillIsochPort::init( 
	IOFireWireBus &			bus, 
	IOByteCount				expectedBytesPerSecond, 
	UInt32					interruptMicroseconds,
	UInt64					channelMask,
	PacketProc				packetProc,
	OSObject *				target )
{
//	IOByteCount bytesNeeded = expectedBytesPerSecond * interruptMicroseconds * 2 / 1000000 ;
//	
//	return initWithBufferSize( bus, bytesNeeded, interruptMicroseconds, channels, packetProc, target ) ;
	return super::init() ;
}

#if 0
bool
IOFWBufferFillIsochPort::initWithBufferSize( 
	IOFireWireBus &			bus, 
	IOByteCount				bufferSize, 
	UInt32					interruptMicroseconds, 
	UInt64					channels,
	PacketProc				packetProc,
	OSObject *				target )
{
	if ( ! super::init() )
	{
		return false ;
	}	
}
#endif

void
IOFWBufferFillIsochPort::free()
{
	delete[] fBackingStore ;
	
	super::free() ;
}

// Return maximum speed and channels supported
// (bit n set = chan n supported)
IOReturn
IOFWBufferFillIsochPort::getSupported (
	IOFWSpeed &			maxSpeed, 
	UInt64 &			chanSupported)
{
    maxSpeed = kFWSpeedMaximum;
    chanSupported = ~(UInt64)0;
    return kIOReturnSuccess;
}

unsigned int
IOFWBufferFillIsochPort::gotIsoch( IOVirtualRange packets[], unsigned int maxPacketCount )
{
	packets = NULL ;
	return 0 ;
}

unsigned int
IOFWBufferFillIsochPort::gotIsochAll( IOVirtualRange packets[], unsigned int maxPacketCount )
{
	packets = NULL ;
	return 0 ;
}

void
IOFWBufferFillIsochPort::pushIsoch()
{
}

IOReturn
IOFWBufferFillIsochPort::setIsochResourceFlags( IOFWIsochResourceFlags flags )
{
	fIsochResourceFlags = flags ;
	
	return kIOReturnSuccess ;
}

IOFWIsochResourceFlags
IOFWBufferFillIsochPort::getIsochResourceFlags() const
{
	return fIsochResourceFlags ; 
}

IOReturn
IOFWBufferFillIsochPort::setFlags( UInt32 flags )
{
	fFlags = flags ; 
	return kIOReturnSuccess ;
}

UInt32
IOFWBufferFillIsochPort::getFlags()
{
	return fFlags ; 
}

IOReturn
IOFWBufferFillIsochPort::setChannels( UInt64 channelMask )
{
	fChannels = channelMask ;
	return kIOReturnSuccess ;
}

AbsoluteTime
IOFWBufferFillIsochPort::getInterruptTime()
{
	return AbsoluteTime() ;
}

