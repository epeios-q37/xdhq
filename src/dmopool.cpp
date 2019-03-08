/*
	Copyright (C) 2017 Claude SIMON (http://zeusw.org/epeios/contact.html).

	This file is part of 'XDHq' software.

    'XDHq' is free software: you can redistribute it and/or modify it
    under the terms of the GNU Affero General Public License as published
    by the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    'XDHq' is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with 'XDHq'.  If not, see <http://www.gnu.org/licenses/>.
*/

# include "dmopool.h"

using namespace dmopool;

#include "prtcl.h"

#include "plugins.h"

#include "registry.h"

#include "bch.h"
#include "crt.h"
#include "csdbns.h"
#include "flx.h"
#include "logq.h"
#include "lstbch.h"
#include "mtk.h"
#include "sclmisc.h"
#include "str.h"


namespace {
	static qCDEF( char *, ProtocolId_, "877c913f-62df-40a1-bf5d-4bb5e66a6dd9" );

	namespace registry_ {
		namespace parameter {
			sclrgstry::rEntry Notification( "DemoNotification", sclrgstry::Parameters );
		}
	}

	qROW( FRow );	// FrontenrdRow.

	typedef lstbch::qLBUNCHd( rShared *, sFRow ) dShareds_;
	qW( Shareds_ );

	qROW( BRow );	// Back-end Row.

	class rBackend_
	{
	private:
		void InvalidAll_( void )
		{
			sFRow Row = Shareds.First();

			while ( Row != qNIL ) {
				Shareds( Row )->Id = Undefined;

				Row = Shareds.Next( Row );
			}
		}
	public:
		sBRow Row;
		fdr::rRWDriver *Driver;
		wShareds_ Shareds;
		mtx::rHandler Access;
		tht::rBlocker Switch;
		bso::sBool GiveUp;
		str::wString IP;
		void reset( bso::sBool P = true )
		{
			if ( P ) {
				if ( Access != mtx::Undefined )
					mtx::Delete( Access, true );

				InvalidAll_();
			}

			Row = qNIL;
			Driver = NULL;
			Shareds.reset( P );
			Access = mtx::Undefined;
			Switch.reset( P );
			tol::reset( P, IP );
			GiveUp = false;	// If at 'true', the client is deemed to be disconnected.
		}
		qCDTOR( rBackend_ );
		void Init(
			sBRow Row,
			fdr::rRWDriver &Driver,
			const str::dString &IP )
		{
			reset();

			this->Row = Row;
			this->Driver = &Driver;
			Shareds.Init();
			Access = mtx::Create();
			Switch.Init();
			this->IP.Init( IP );
			GiveUp = false;
		}
		bso::sBool Set( rShared &Shared )
		{
			sFRow Row = Shareds.New();

			if ( *Row < Max ) {
				Shareds.Store( &Shared, Row );

				Shared.Id = (sId)*Row;
				Shared.Driver = Driver;
				Shared.Switch = &Switch;

				return true;
			} else
				return false;
		}
	};

	mtx::rHandler MutexHandler_ = mtx::Undefined;
	crt::qMCRATEw( str::dString, sBRow ) Tokens_;
	crt::qMCRATEw( str::dString, sBRow ) Heads_;
	bch::qBUNCHw( rBackend_ *, sBRow ) Backends_;
	csdbns::rListener Listener_;

	// NOTA : TU : Thread Unsafe ; TS : Thread Safe.

	sBRow TUGetBackendRow_( const str::dString &Token )
	{
		if ( !mtx::IsLocked( MutexHandler_ ) )
			qRGnr();

		sBRow Row = Tokens_.First();

		while ( (Row != qNIL) && ( Tokens_( Row ) != Token) )
			Row = Tokens_.Next( Row );

		return Row;
	}

	rBackend_ *TUGetBackend_( const str::dString &Token )
	{
		sBRow Row = TUGetBackendRow_( Token );

		if ( Row != qNIL )
			return Backends_( Row );
		else
			return NULL;
	}

	rBackend_ *TSGetBackend_( const str::dString &Token )
	{
		rBackend_ *Backend = NULL;
	qRH;
		mtx::rMutex Mutex;
	qRB;
		Mutex.InitAndLock( MutexHandler_ );

		Backend = TUGetBackend_( Token );
	qRR;
	qRT;
	qRE;
		return Backend;
	}

	const str::dString &TUGetHead_(
		const str::dString &Token,
		str::dString &Head )
	{
		sBRow Row = TUGetBackendRow_( Token );

		if ( Row != qNIL )
			Heads_.Recall( Row, Head );

		return Head;
	}

	const str::dString &TSGetHead_(
		const str::dString &Token,
		str::dString &Head )
	{
	qRH;
		mtx::rMutex Mutex;
	qRB;
		Mutex.InitAndLock( MutexHandler_ );

		TUGetHead_( Token, Head );
	qRR;
	qRT;
	qRE;
		return Head;
	}

	void Remove_( sBRow Row )
	{
	qRH;
		mtx::rMutex Mutex;
	qRB;
		Mutex.Init( MutexHandler_ );

		if ( !Backends_.Exists( Row ) || !Tokens_.Exists( Row ) || !Heads_.Exists( Row ) )
			qRGnr();

		Backends_.Remove( Row );
		Tokens_.Remove( Row );
		Heads_.Remove( Row );
	qRR;
	qRT;
	qRE;
	}

	rBackend_ *Create_(
		fdr::rRWDriver &Driver,
		const str::dString &IP,
		const str::dString &Token,
		const str::dString &Head )
	{
		rBackend_ *Backend = NULL;
	qRH;
		mtx::rMutex Mutex;
		sBRow Row = qNIL;
	qRB;
		Mutex.InitAndLock( MutexHandler_) ;

		Row = TUGetBackendRow_( Token );

		if ( Row == qNIL ) {
			Row = Tokens_.Append( Token );

			if ( Row != Backends_.New() )
				qRGnr();

			if ( Row != Heads_.New() )
				qRGnr();
		} else
			delete Backends_( Row );

		if ( (Backend = new rBackend_) == NULL )
			qRAlc();

		Backend->Init( Row, Driver, IP );

		Backends_.Store( Backend, Row );

		Heads_.Store( Head, Row );
	qRR;
		if ( Backend != NULL )
			delete Backend;
	qRT;
	qRE;
		return Backend;
	}

	void Get_(
		flw::rRFlow &Flow,
		str::dString &String )
	{
		prtcl::Get( Flow, String );
	}

	void Put_(
		const str::dString &String,
		flw::rWFlow &Flow )
	{
		prtcl::Put( String, Flow );
	}

	void Put_(
		const char *String,
		flw::rWFlow &Flow )
	{
		prtcl::Put( String, Flow );
	}

	namespace token_ {
		plgn::rRetriever<plugins::cToken> PluginRetriever_;

		class sToken_
		: public plugins::cToken
		{
		protected:
			plugins::eStatus PLUGINSHandle(
				const str::dString &Raw,
				str::dString &Normalized ) override
			{
				tol::bUUID UUID;

				Normalized = Raw;

				if ( Raw.Amount() == 0 )
					Normalized.Append( tol::UUIDGen( UUID ) );

				return plugins::sOK;
			}
		public:
			void reset(bso::sBool = true ) {}
			qCVDTOR( sToken_ );
			void Init( void ) {}
		} DefaultHandler_;

		plugins::cToken &GetPlugin( void )
		{
			PluginRetriever_.Init();

			if ( sclmisc::Plug( plugins::TokenPluginTarget, NULL, PluginRetriever_, qRPU ) )
				return PluginRetriever_.Plugin();
			else {
				DefaultHandler_.Init();
				return DefaultHandler_;
			}
		}
	}

	void Notify_(
		const char *Message,
		flw::rWFlow &Flow )
	{
	qRH;
		str::wString Notification;
	qRB;
		Notification.Init( Message );

		if ( Notification.IsEmpty() )
			sclmisc::OGetValue( registry_::parameter::Notification, Notification );

		prtcl::Put( Notification, Flow );
	qRR;
	qRT;
	qRE;
	}

	void Handshake_( fdr::rRWDriver &Driver )
	{
	qRH;
		flw::rDressedRWFlow<> Flow;
	qRB;
		Flow.Init( Driver );

		switch ( csdcmn::GetProtocolVersion( ProtocolId_, Flow ) ) {
		case 0:
			Put_( "", Flow );
			Notify_( NULL, Flow );
			Flow.Commit();
			break;
		case csdcmn::UndefinedVersion:
			Put_( "Unknown demo protocol !!!", Flow );
			Flow.Commit();
			qRGnr();
		default:
			Put_( "Unknown demo version !!!", Flow );
			Flow.Commit();
			qRGnr();
			break;
		}
	qRR;
	qRT;
	qRE;
	}

	rBackend_ *CreateBackend_(
		fdr::rRWDriver &Driver,
		const str::dString &IP )
	{
		rBackend_ *Backend = NULL;
	qRH;
		flw::rDressedRWFlow<> Flow;
		str::wString Token, Head, ErrorMessageLabel, ErrorMessage;
		plugins::eStatus Status = plugins::s_Undefined;
	qRB;
		Flow.Init( Driver );

		Token.Init();
		Get_( Flow, Token );

		switch ( Status = token_::GetPlugin().Handle( Token ) ) {
		case plugins::sOK:
			Head.Init();
			Get_( Flow, Head );

			Backend = Create_( Driver, IP, Token, Head );
			break;
		default:
			Token.Init();	// To report an error (see below).
			ErrorMessageLabel.Init( "PLUGINS_" );
			ErrorMessageLabel.Append( plugins::GetLabel( Status ) );
			ErrorMessage.Init();
			sclmisc::GetBaseTranslation( ErrorMessageLabel, ErrorMessage );
			break;
		}

		Put_( Token, Flow );

		if ( Backend == NULL )
			Put_( ErrorMessage, Flow );
	qRR;
		if ( Backend != NULL )
			delete Backend;

		Backend = NULL;
	qRT;
	qRE;
		return Backend;
	}

	void HandleSwitching_(
		fdr::rRWDriver &Driver,
		const dShareds_ &Shareds,
		tht::rBlocker &Blocker )
	{
	qRH;
		flw::rDressedRFlow<> Flow;
		sId Id = Undefined;
	qRB;
		Flow.Init( Driver );

		while ( true ) {
			Id = Undefined;

			Id = GetId( Flow );

			if ( !Shareds.Exists( Id ) ) {
				Id = Undefined;
				qRGnr();
			}

			Shareds( Id )->Read.Unblock();

			Blocker.Wait();	// Waits until all data in flow red.
		}
	qRR;
	qRT;
	qRE;
	}

	struct gConnectionData_
	{
		sck::sSocket Socket = sck::Undefined;
		const char *IP = NULL;
	};

	void NewConnexion_(
		gConnectionData_ &Data,
		mtk::gBlocker &Blocker )
	{
	qRH;
		sck::sSocket Socket = sck::Undefined;
		str::wString IP;
		mtx::rMutex Mutex;
		sck::rRWDriver Driver;
		rBackend_ *Backend = NULL;
	qRB;
		Socket = Data.Socket;
		IP.Init( Data.IP );

		Blocker.Release();

		Driver.Init( Socket, false, fdr::ts_Default );

		Handshake_( Driver );

		if ( ( Backend = CreateBackend_( Driver, IP ) ) != NULL )
			HandleSwitching_( Driver, Backend->Shareds, Backend->Switch );	// Don't return until disconnection or error.
	qRR;
	qRT;
		if ( Backend != NULL ) {
			Remove_( Backend->Row );
			delete Backend;

			Backend = NULL;
		}

		Driver.reset();	// Otherwise it will be done after the destruction of the socket, hence the commit will fail.

		if ( Socket != sck::Undefined ) {
			sck::Close( Socket, err::hUserDefined );	// An error occurring during closing is ignored.
			Socket = sck::Undefined;
		}
	qRE( sclmisc::ErrFinal() );
	}

	void NewConnexionRoutine_(
		gConnectionData_ &Data,
		mtk::gBlocker &Blocker )
	{
	qRFH;
	qRFB;
	NewConnexion_( Data, Blocker );
	qRFR;
	qRFT;
	qRFE( sclmisc::ErrFinal() );
	}

	void ListeningRoutine_( void * )
	{
	qRFH;
		gConnectionData_ Data;
	qRFB;
		while ( true ) {
			Data.Socket = Listener_.GetConnection( Data.IP );

			mtk::Launch( NewConnexionRoutine_, Data );
		}
	qRFR;
	qRFT;
	qRFE( sclmisc::ErrFinal() );
	}
}

void dmopool::Initialize( void )
{
qRH;
	csdbns::sService Service = csdbns::Undefined;
qRB;
	if ( (Service = sclmisc::OGetU16( registry::parameter::DemoService, csdbns::Undefined ) ) != csdbns::Undefined ) {
		Listener_.Init( Service );

		mtk::RawLaunch( ListeningRoutine_, NULL );
	}
qRR;
qRT;
qRE;
}

bso::sBool dmopool::GetConnection(
	const str::dString &Token,
	str::dString &IP,
	rShared &Shared )
{
	rBackend_ *Backend = NULL;
qRH;
	mtx::rMutex Mutex;
	flw::rDressedWFlow<> Flow;
qRB;
	Backend = TSGetBackend_( Token );

	if ( Backend != NULL ) {
		Mutex.Init( Backend->Access );

		Mutex.Lock();

		if ( Backend->Set( Shared ) ) {
			Flow.Init( *Backend->Driver );
			IP.Append( Backend->IP );
			PutId( Undefined, Flow );	// To signal to the back-end a new connection.
			PutId( Shared.Id, Flow );	// The id of the new front-end.
			Flow.Commit();
		}  else
			Backend = NULL;
	}
qRR;
qRT;
qRE;
	return Backend != NULL;
}

namespace {
	void GetHead_(
		void *UP,
		str::dString &Head )
	{
		TSGetHead_( *(const str::wString *)UP, Head );	// 'UP' contains the token.
	}
}


qGCTOR( dmopool )
{
	MutexHandler_ = mtx::Create();
	Tokens_.Init();
	Heads_.Init();
	Backends_.Init();
	sclxdhtml::SetHeadFunction( GetHead_ );
}

qGDTOR( dmopool )
{
	if ( MutexHandler_ != mtx::Undefined )
		mtx::Delete( MutexHandler_, true );

	sBRow Row = Backends_.First();

	while ( Row != qNIL ) {
		delete Backends_( Row );

		Row = Backends_.Next( Row );
	}
}

