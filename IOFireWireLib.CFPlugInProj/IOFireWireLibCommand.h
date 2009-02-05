/*
 * Copyright (c) 1998-2000 Apple Computer, Inc. All rights reserved.
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
 *  IOFireWireLibCommand.h
 *  IOFireWireLib
 *
 *  Created by NWG on Tue Dec 12 2000.
 *  Copyright (c) 2000 Apple Computer, Inc. All rights reserved.
 *
 */

#import <IOKit/firewire/IOFireWireFamilyCommon.h>

//#import "IOFireWireLibDevice.h"
#import "IOFireWireLibIUnknown.h"
#import "IOFireWireLib.h"
#import "IOFireWireLibPriv.h"

namespace IOFireWireLib {

	typedef ::IOFireWireLibCommandCallback	CommandCallback ;

	class Device ;
	class Cmd: public IOFireWireIUnknown
	{		
		public:
		
			Cmd( const IUnknownVTbl & vtable, Device & userClient, io_object_t device,  const FWAddress & inAddr, 
					CommandCallback callback, const bool failOnReset, const UInt32 generation, 
					void * refCon, CommandSubmitParams* params ) ;
			virtual ~Cmd() ;
		
		public:
		
			virtual HRESULT 		QueryInterface( REFIID iid, LPVOID* ppv ) ;
			virtual void			SetTarget( const FWAddress&	addr) ;
			virtual void			SetGeneration( UInt32 generation) ;
			virtual void			SetCallback( CommandCallback callback ) ;
			virtual IOReturn		Submit() = 0 ;
			virtual IOReturn		Submit( CommandSubmitParams* params, mach_msg_type_number_t paramsSize,
											CommandSubmitResult* ioResult, mach_msg_type_number_t* ioResultSize ) ;
			virtual IOReturn		SubmitWithRefconAndCallback( void* refCon, CommandCallback inCallback ) ;
			virtual IOReturn		Cancel( IOReturn reason) ;
			virtual void			SetBuffer( UInt32 size, void* buf ) ;
			virtual void			GetBuffer( UInt32* outSize, void** outBuf ) ;
			virtual IOReturn		SetMaxPacket( IOByteCount maxBytes ) ;
			virtual void			SetFlags( UInt32 inFlags ) ;
			static void				CommandCompletionHandler( void* refcon, IOReturn result, IOByteCount bytesTransferred ) ;									

			// --- getters -----------------------
			static IOReturn			SGetStatus( IOFireWireLibCommandRef	self) ;
			static UInt32			SGetTransferredBytes( IOFireWireLibCommandRef	self) ;
			static void				SGetTargetAddress( IOFireWireLibCommandRef self, FWAddress* outAddr ) ;

			// --- setters -----------------------
			static void				SSetTarget ( IOFireWireLibCommandRef self, const FWAddress * addr ) ;
			static void				SSetGeneration ( IOFireWireLibCommandRef self, UInt32 generation ) ;
			static void				SSetCallback ( IOFireWireLibCommandRef self, CommandCallback callback ) ;
			static void				SSetRefCon ( IOFireWireLibCommandRef self, void * refCon ) ;
		
			static const Boolean	SIsExecuting ( IOFireWireLibCommandRef self ) ;
			static IOReturn			SSubmit ( IOFireWireLibCommandRef self ) ;
			static IOReturn			SSubmitWithRefconAndCallback ( IOFireWireLibCommandRef self, void * refCon,
											CommandCallback callback) ;
			static IOReturn			SCancel ( IOFireWireLibCommandRef self, IOReturn reason ) ;
			static void				SSetBuffer ( IOFireWireLibCommandRef self, UInt32 size, void * buf ) ;
			static void				SGetBuffer ( IOFireWireLibCommandRef self, UInt32 * outSize, void ** outBuf) ;
			static IOReturn			SSetMaxPacket ( IOFireWireLibCommandRef self, IOByteCount maxBytes ) ;
			static void				SSetFlags ( IOFireWireLibCommandRef self, UInt32 flags ) ;

		protected:
		
			static IOFireWireCommandInterface	sInterface ;
	
		protected:
		
			Device &						mUserClient ;
			io_object_t						mDevice ;
			io_async_ref_t					mAsyncRef ;
			IOByteCount						mBytesTransferred ;	
			Boolean							mIsExecuting ;
			IOReturn						mStatus ;
			void*							mRefCon ;
			CommandCallback					mCallback ;
			
			CommandSubmitParams* 			mParams ;
	} ;

#pragma mark -
	class ReadCmd: public Cmd
	{
		protected:
			typedef ::IOFireWireReadCommandInterface	Interface ;
			typedef ::IOFireWireLibReadCommandRef		CmdRef ;
		
		public:
									ReadCmd(
											Device& 						userclient,
											io_object_t 					device,
											const FWAddress& 				addr,
											void* 							buf,
											UInt32 							size, 
											CommandCallback 	callback, 
											bool							failOnReset, 
											UInt32 							generation,
											void* 							inRefCon ) ;
			virtual					~ReadCmd()										{}
			virtual HRESULT 		QueryInterface(REFIID iid, LPVOID* ppv) ;	
			inline ReadCmd*		 	GetThis(CmdRef	self)		{ return IOFireWireIUnknown::InterfaceMap<ReadCmd>::GetThis(self) ; }
			static IUnknownVTbl**	Alloc(
											Device& 						userclient,
											io_object_t						device,
											const FWAddress&				addr,
											void*							buf,
											UInt32							size,
											CommandCallback 	callback,
											bool							failOnReset,
											UInt32							generation,
											void*							inRefCon) ;

			virtual IOReturn		Submit() ;

		private:
			static Interface 	sInterface ;
	
	} ;

#pragma mark -
	class ReadQuadCmd: public Cmd
	{	
		protected:
			typedef ::IOFireWireReadQuadletCommandInterface		Interface ;
			typedef ::IOFireWireLibReadQuadletCommandRef		CmdRef ;

		public:
										ReadQuadCmd(
												Device& 						userclient,
												io_object_t						device,
												const FWAddress &				addr,
												UInt32							quads[],
												UInt32							numQuads,
												CommandCallback 	callback,
												Boolean							failOnReset,
												UInt32							generation,
												void*							refcon) ;
			virtual						~ReadQuadCmd() {}

			virtual HRESULT 			QueryInterface( REFIID iid, LPVOID* ppv ) ;
			inline static ReadQuadCmd*	GetThis( CmdRef self )		{ return IOFireWireIUnknown::InterfaceMap<ReadQuadCmd>::GetThis(self) ; }
										
			virtual void				SetFlags( UInt32 inFlags ) ;
			virtual void				SetQuads( UInt32 quads[], UInt32 numQuads) ;
			virtual IOReturn			Submit() ;		

			// static
			static IUnknownVTbl**		Alloc(
												Device& inUserClient,
												io_object_t			device,
												const FWAddress &	addr,
												UInt32				quads[],
												UInt32				numQuads,
												CommandCallback callback,
												Boolean				failOnReset,
												UInt32				generation,
												void*				inRefCon) ;
			static void					SSetQuads(
											IOFireWireLibReadQuadletCommandRef self,
											UInt32					inQuads[],
											UInt32					inNumQuads) ;
			static void				CommandCompletionHandler(
											void*					refcon,
											IOReturn				result,
											void*					quads[],
											UInt32					numQuads) ;		
		protected:
			static Interface	sInterface ;
			unsigned int		mNumQuads ;
	} ;

#pragma mark -
	class WriteCmd: public Cmd
	{
		protected:
			typedef ::IOFireWireWriteCommandInterface 	Interface ;
	
		public:
/*			virtual Boolean			Init(	
											const FWAddress&	inAddr,
											void*				buf,
											UInt32				size,
											CommandCallback	inCallback,
											const Boolean		inFailOnReset,
											const UInt32		inGeneration,
											void*				inRefCon ) ;*/
									WriteCmd(
											Device& 			userclient, 
											io_object_t 		device, 
											const FWAddress& 	addr, 
											void* 				buf, 
											UInt32 				size, 
											CommandCallback callback, 
											bool 				failOnReset, 
											UInt32 				generation, 
											void* 				inRefCon ) ;
			virtual					~WriteCmd() {}
			static IUnknownVTbl**	Alloc(
											Device& inUserClient,
											io_object_t			device,
											const FWAddress &	addr,
											void*				buf,
											UInt32				size,
											CommandCallback callback,
											bool				failOnReset,
											UInt32				generation,
											void*				inRefCon) ;
			virtual HRESULT 		QueryInterface(REFIID iid, LPVOID* ppv) ;
			inline static WriteCmd* GetThis(IOFireWireLibWriteCommandRef self)		{ return IOFireWireIUnknown::InterfaceMap<WriteCmd>::GetThis(self) ; }
	
			// required Submit() method
			virtual IOReturn		Submit() ;		

		protected:
			static Interface		sInterface ;
	} ;

#pragma mark -
	class WriteQuadCmd: public Cmd
	{
		protected:
			typedef ::IOFireWireWriteQuadletCommandInterface	Interface ;
			typedef ::IOFireWireLibWriteQuadletCommandRef		CmdRef ;
		public:
										WriteQuadCmd(
												Device& 			userclient, 
												io_object_t 		device, 
												const FWAddress& 	addr, 
												UInt32 				quads[], 
												UInt32 				numQuads,
												CommandCallback 	callback, 
												bool 				failOnReset, 
												UInt32 				generation, 
												void* 				inRefCon ) ;
			virtual						~WriteQuadCmd() ;
			static IUnknownVTbl**	Alloc( Device& userclient, io_object_t device, const FWAddress& addr, 
												UInt32 quads[], UInt32 numQuads, CommandCallback callback, 
												bool failOnReset, UInt32 generation, void* refcon) ;
		

			virtual HRESULT 			QueryInterface(REFIID iid, LPVOID* ppv) ;
			inline static WriteQuadCmd*	GetThis(CmdRef self)	{ return IOFireWireIUnknown::InterfaceMap<WriteQuadCmd>::GetThis(self) ; }

			virtual void				SetFlags( UInt32 inFlags ) ;
			virtual void				SetQuads( UInt32 inQuads[], UInt32 inNumQuads) ;
			virtual IOReturn 			Submit() ;

			// static
			static void					SSetQuads(
											CmdRef		 self,
											UInt32				inQuads[],
											UInt32				inNumQuads) ;
		
		protected:
			static Interface	sInterface ;
			UInt8*											mParamsExtra ;
	} ;

#pragma mark -
	class CompareSwapCmd: public Cmd
	{
		protected:
			typedef ::IOFireWireLibCompareSwapCommandRef 		CmdRef ;
			typedef ::IOFireWireCompareSwapCommandInterface		Interface ;
	
			// --- ctor/dtor ----------------
											CompareSwapCmd(	
													Device& 						inUserClient, 
													io_object_t 					device, 
													const FWAddress & 				addr, 
													UInt64 							cmpVal, 
													UInt64 							newVal,
													unsigned int					quads,
													CommandCallback				 	callback, 
													bool							failOnReset, 
													UInt32 							generation, 
													void* 							inRefCon) ;
			virtual							~CompareSwapCmd() ;	
			virtual HRESULT 				QueryInterface(REFIID iid, LPVOID* ppv) ;
			inline static CompareSwapCmd* 	GetThis(IOFireWireLibCompareSwapCommandRef self)		{ return IOFireWireIUnknown::InterfaceMap<CompareSwapCmd>::GetThis(self) ; }
									
			virtual void					SetFlags( UInt32 inFlags ) ;
			void							SetValues( UInt32 cmpVal, UInt32 newVal) ;
			virtual IOReturn				SetMaxPacket(
													IOByteCount				inMaxBytes) ;
			virtual IOReturn 				Submit() ;
	
		// --- v2 ---
			void							SetValues( UInt64 cmpVal, UInt64 newVal) ;
			Boolean							DidLock() ;
			IOReturn 						Locked( UInt32* oldValue) ;
			IOReturn 						Locked( UInt64* oldValue) ;

		//
		// static interface
		//
		public:
			static IUnknownVTbl**	Alloc(
											Device& 			userclient,
											io_object_t			device,
											const FWAddress &	addr,
											UInt64				cmpVal,
											UInt64				newVal,
											unsigned int		quads,
											CommandCallback		callback,
											bool				failOnReset,
											UInt32				generation,
											void*				inRefCon) ;
			static void				SSetValues(
											IOFireWireLibCompareSwapCommandRef self,
											UInt32				cmpVal,
											UInt32				newVal) ;
			static void				SSetValues64(
											CmdRef			 	self, 
											UInt64 				cmpVal, 
											UInt64 				newVal) ;
			static Boolean			SDidLock(
											CmdRef				self) ;
			static IOReturn			SLocked(
											CmdRef 				self, 
											UInt32* 			oldValue) ;
			static IOReturn			SLocked64(
											CmdRef				self, 
											UInt64* 			oldValue) ;
			static void				SSetFlags( CmdRef self, UInt32 inFlags) ;
			static void				CommandCompletionHandler(
											void*				refcon,
											IOReturn			result,
											void*				quads[],
											UInt32				numQuads) ;
											
		
		protected:
			static Interface				sInterface ;
			UInt8*							mParamsExtra ;
			CompareSwapSubmitResult			mSubmitResult ;
	} ;
}
