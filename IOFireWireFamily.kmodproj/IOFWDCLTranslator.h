/*
 * Copyright (c) 1998-2002 Apple Computer, Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 * 
 * Copyright (c) 1999-2003 Apple Computer, Inc.  All Rights Reserved.
 * 
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 * 
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 * 
 * @APPLE_LICENSE_HEADER_END@
 */
/*
 * Copyright (c) 1999-2002 Apple Computer, Inc.  All rights reserved.
 *
 * A DCL program to interpret (in software) a program that's too complicated
 * for the DMA engine.
 *
 * HISTORY
 *
 */


#ifndef _IOKIT_IOFWDCLTRANSLATOR_H
#define _IOKIT_IOFWDCLTRANSLATOR_H

#include <libkern/c++/OSObject.h>
#include <IOKit/firewire/IOFWDCLProgram.h>

class IODCLTranslator : public IODCLProgram
{
    OSDeclareAbstractStructors(IODCLTranslator)

protected:
    enum
    {
        kNumPingPongs				= 2,
        kNumPacketsPerPingPong		= 500,
        kMaxIsochPacketSize			= 1000,
        kPingPongBufferSize			= kNumPingPongs * kNumPacketsPerPingPong * kMaxIsochPacketSize
    };

    // Opcodes and buffer for pingpong program
    DCLLabel			fStartLabel;
    DCLTransferPacket	fTransfers[kNumPingPongs*kNumPacketsPerPingPong];
    DCLCallProc			fCalls[kNumPingPongs];
    DCLJump				fJumpToStart;
    UInt8				fBuffer[kPingPongBufferSize];

    IODCLProgram *		fHWProgram;				// Hardware program executing our opcodes
    DCLCommand*			fToInterpret;			// The commands to interpret
    DCLCommand*			fCurrentDCLCommand;		// Current command to interpret
    int					fPingCount;				// Are we pinging or ponging?
    UInt32				fPacketHeader;

    static void ListeningDCLPingPongProc(DCLCommand* pDCLCommand);
    static void TalkingDCLPingPongProc(DCLCommand* pDCLCommand);

public:
    virtual bool init(DCLCommand* toInterpret);
    virtual IOReturn allocateHW(IOFWSpeed speed, UInt32 chan);
    virtual IOReturn releaseHW();
    virtual IOReturn notify(UInt32 notificationType,
	DCLCommand** dclCommandList, UInt32 numDCLCommands);
    virtual void stop();

    DCLCommand* getTranslatorOpcodes();
    void setHWProgram(IODCLProgram *program);
};

class IODCLTranslateTalk : public IODCLTranslator
{
    OSDeclareDefaultStructors(IODCLTranslateTalk)

protected:

public:
    virtual IOReturn compile(IOFWSpeed speed, UInt32 chan);
    virtual IOReturn start();

};
class IODCLTranslateListen : public IODCLTranslator
{
    OSDeclareDefaultStructors(IODCLTranslateListen)

protected:

public:
    virtual IOReturn compile(IOFWSpeed speed, UInt32 chan);
    virtual IOReturn start();

};
#endif /* ! _IOKIT_IOFWDCLPROGRAM_H */

