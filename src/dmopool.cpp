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

#include "registry.h"

#include "bch.h"
#include "crt.h"
#include "csdbns.h"
#include "flx.h"
#include "mtk.h"
#include "sclmisc.h"
#include "str.h"


namespace {

	class rConnections_
	{
	private:
		void Close_( void )
		{
			sdr::sRow Row = Sockets.First();

			while ( Row != qNIL ) {
				sck::Close( Sockets( Row ) );

				Row = Sockets.Next( Row );
			}
		}
	public:
		bch::qBUNCHwl( sck::sSocket ) Sockets;
		mtx::rHandler MutexHandler = mtx::Undefined;
		void reset( bso::sBool P = true )
		{
			if ( P ) {
				if ( MutexHandler != mtx::Undefined )
					mtx::Delete( MutexHandler );
				Close_();
			}

			MutexHandler = mtx::Undefined;
			tol::reset( P, Sockets );
		}
		qCDTOR( rConnections_ );
		void Init( void )
		{
			reset();

			MutexHandler = mtx::Create();
			Sockets.Init();
		}
	};

	mtx::rHandler MutexHandler_ = mtx::Undefined;
	qROW( Row );
	crt::qMCRATEw( str::dString, sRow ) Tokens_;
	bch::qBUNCHw( rConnections_ *, sRow ) Clients_;
	csdbns::rListener Listener_;

	rConnections_ *TUSearch_( const str::dString &Token )
	{
		if ( !mtx::IsLocked( MutexHandler_ ) )
			qRGnr();

		sRow Row = Tokens_.First();

		while ( ( Row != qNIL ) && (Tokens_( Row ) != Token) )
			Row = Tokens_.Next( Row );

		if ( Row != qNIL )
			return Clients_( Row );
		else
			return NULL;
	}

	rConnections_ *TSSearch_( const str::dString &Token )
	{
		rConnections_ *Connections = NULL;
	qRH;
		mtx::rMutex Mutex;
	qRB;
		Mutex.InitAndLock( MutexHandler_ );

		Connections = TUSearch_( Token );
	qRR;
	qRT;
	qRE;
		return Connections;
	}

	rConnections_ *Create_( const str::dString &Token )
	{
		rConnections_*Connections = NULL;
	qRH;
		mtx::rMutex Mutex;
		sRow Row = qNIL;
	qRB;
		Mutex.InitAndLock( MutexHandler_) ;

		if ( TUSearch_( Token ) != NULL )
			qRGnr();

		Row = Tokens_.Append( Token );

		if ( (Connections = new rConnections_) == NULL )
			qRAlc();

		Connections->Init();

		if ( Clients_.Append( Connections ) != Row )
			qRGnr();
	qRR;
		if ( Connections != NULL )
			delete Connections;
	qRT;
	qRE;
		return Connections;
	}

	void Get_(
		flw::sRFlow &Flow,
		str::dString &String )
	{
		prtcl::Get( Flow, String );
	}

	void Put_(
		const str::dString &String,
		flw::sWFlow &Flow )
	{
		prtcl::Put( String, Flow );
	}

	void NewConnexionRoutine_(
		void *UP,
		mtk::gBlocker &Blocker )
	{
	qRFH;
		sck::sSocket Socket = *(sck::sSocket *)UP;
		str::wString Token;
		sck::rRWFlow Flow;
		tol::bUUID UUID;
		rConnections_ *Connections = NULL;
		mtx::rMutex Mutex;
	qRFB;
		Blocker.Release();

		Flow.Init( Socket, false, sck::NoTimeout );

		Token.Init();
		Get_( Flow, Token );

		if ( Token.Amount() == 0 ) {
			Token.Append( tol::UUIDGen( UUID ) );

			Connections = Create_( Token );
		} else {
			Connections = TSSearch_( Token );
		}

		if ( Connections == NULL )
			Token.Init();
		else {
			Mutex.InitAndLock( Connections->MutexHandler );

			Connections->Sockets.Push( Socket );

			Mutex.Unlock();
		}

		Put_( Token, Flow );
	qRFR;
	qRFT;
	qRFE( sclmisc::ErrFinal() );
	}

	void ListeningRoutine_( void * )
	{
	qRFH;
		sck::sSocket Socket = sck::Undefined;
		const char *IP;
	qRFB;
		while ( true ) {
			Socket = sck::Undefined;

			Socket = Listener_.GetConnection( IP );

			mtk::Launch( NewConnexionRoutine_, &Socket );
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

sck::sSocket dmopool::GetConnection( const str::dString &Token )
{
	sck::sSocket Socket = sck::Undefined;
qRH;
	mtx::rMutex Mutex;
	rConnections_ *Connections = NULL;
	bso::u8__ Counter = 100;	// Combined with the 'Suspend(...)' below,
								// this give ten seconds to the client to respond.
qRB;
	Connections = TSSearch_( Token );

	if ( Connections == NULL )
		qRGnr();

	Mutex.InitAndLock( Connections->MutexHandler );

	while ( !Connections->Sockets.Amount() ) {
		Mutex.Unlock();

		if ( !--Counter )
			qRGnr();

		tht::Suspend( 100 );

		Mutex.Lock();
	}

	Socket = Connections->Sockets.Pop();
qRR;
qRT;
qRE;
	return Socket;
}

qGCTOR( dmopool )
{
	MutexHandler_ = mtx::Create();
	Tokens_.Init();
	Clients_.Init();
}

qGDTOR( dmopool )
{
	if ( MutexHandler_ != mtx::Undefined )
		mtx::Delete( MutexHandler_, true );

	sRow Row = Clients_.First();

	while ( Row != qNIL ) {
		delete Clients_( Row );

		Row = Clients_.Next( Row );
	}
}

