//
// This file is part of libGDF.
//
// libGDF is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation, either version 3 of
// the License, or (at your option) any later version.
//
// libGDF is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with libGDF.  If not, see <http://www.gnu.org/licenses/>.
//
// Copyright 2010 Martin Billinger

#include "GDF/RecordBuffer.h"
#include "GDF/Record.h"
#include "GDF/GDFHeaderAccess.h"
#include "GDF/tools.h"
//#include <iostream>

namespace gdf
{
    RecordBuffer::RecordBuffer( const GDFHeaderAccess *gdfh ) : m_gdfh(gdfh)
    {
    }

    //===================================================================================================
    //===================================================================================================

    RecordBuffer::~RecordBuffer( )
    {
        //std::cout << "~RecordBuffer( )" << std::endl;
    }

    //===================================================================================================
    //===================================================================================================

    void RecordBuffer::reset( )
    {
        size_t M = m_gdfh->getMainHeader_readonly( ).get_num_signals( );
        m_records.clear( );
        m_records_full.clear( );
        m_channelhead.resize( M );
        for( size_t i=0; i<M; i++ )
            m_channelhead[i] =m_records.begin( );
    }

    //===================================================================================================
    //===================================================================================================

    void RecordBuffer::handleChannelFull( const size_t channel_idx )
    {
        //std::cout << "Channel Full" << std::endl;
        Record *r = m_channelhead[channel_idx]->get( );
        m_channelhead[channel_idx]++;
        if( r->isFull() )
            handleRecordFull( );
    }

    //===================================================================================================
    //===================================================================================================

    void RecordBuffer::handleRecordFull( )
    {
        //std::cout << "Record Full" << std::endl;
        m_records_full.push_back( m_records.front() );
        m_records.pop_front( );

        std::list<RecordFullHandler*>::iterator it = m_recfull_callbacks.begin( );
        for( ; it != m_recfull_callbacks.end(); it++ )
            (*it)->triggerRecordFull( m_records_full.back().get() );
    }

    //===================================================================================================
    //===================================================================================================

    void RecordBuffer::registerRecordFullCallback( RecordFullHandler *h )
    {
        m_recfull_callbacks.push_back( h );
    }

    //===================================================================================================
    //===================================================================================================

    void RecordBuffer::unregisterRecordFullCallback( RecordFullHandler *h )
    {
        m_recfull_callbacks.remove( h );
    }

    //===================================================================================================
    //===================================================================================================

    void RecordBuffer::addSamplePhys( const size_t channel_idx, const double value )
    {
        Channel *ch = getValidChannel( channel_idx );
        ch->addSamplePhys( value );
        if( ch->getFree( ) == 0 )
            handleChannelFull( channel_idx );
    }

    //===================================================================================================
    //===================================================================================================

    void RecordBuffer::blitSamplesPhys( const size_t channel_idx, const double *values, size_t num )
    {
        size_t i = 0;
        while( i<num )
        {
            Channel *ch = getValidChannel( channel_idx );
            size_t n = std::min( num-i, ch->getFree( ) );
            ch->blitSamplesPhys( &values[i], n );
            if( ch->getFree( ) == 0 )
                handleChannelFull( channel_idx );
            i += n;
        }
    }

    //===================================================================================================
    //===================================================================================================

    void RecordBuffer::addRecord( Record &r )
    {
        if( getNumPartialRecords( ) > 0 )
            throw exception::invalid_operation( "RecordBuffer::addRecord called, but buffer contains partial records." );
        m_records_full.push_back( boost::shared_ptr<Record>( new Record(r) ) );
    }

    //===================================================================================================
    //===================================================================================================

    std::list< boost::shared_ptr<Record> >::iterator RecordBuffer::createNewRecord( )
    {
        m_records.push_back( boost::shared_ptr<Record>( new Record(m_gdfh) ) );
        std::list< boost::shared_ptr<Record> >::iterator it = m_records.end( );
        it--;
        return it;
    }

    //===================================================================================================
    //===================================================================================================

    Record *RecordBuffer::getFirstFullRecord( )
    {
        if( m_records_full.size() > 0 )
            return m_records_full.front( ).get( );
        else
            return NULL;
    }

    //===================================================================================================
    //===================================================================================================

    void RecordBuffer::removeFirstFullRecord( )
    {
        m_records_full.pop_front( );
    }

    //===================================================================================================
    //===================================================================================================

    size_t RecordBuffer::getNumFullRecords( )
    {
        return m_records_full.size( );
    }

    //===================================================================================================
    //===================================================================================================

    size_t RecordBuffer::getNumPartialRecords( )
    {
        return m_records.size( );
    }

    //===================================================================================================
    //===================================================================================================

    Channel *RecordBuffer::getValidChannel( const size_t channel_idx )
    {
        if( channel_idx >= m_channelhead.size( ) )
            throw exception::nonexistent_channel_access( "channel "+boost::lexical_cast<std::string>(channel_idx) +"does not exist" );

        if( m_channelhead[channel_idx] == m_records.end() )
        {
            if( m_records.size() > 0 )
            {
                if( m_records.back()->getChannel(channel_idx)->getFree( ) == 0 )
                    m_channelhead[channel_idx] = createNewRecord( );
                else
                    throw exception::corrupt_recordbuffer( "DOOM is upon us!" );
            }
            else
            {
                // list was empty: set all channel heads to the new record.
                std::list< boost::shared_ptr<Record> >::iterator r = createNewRecord( );
                for( size_t i=0; i<m_channelhead.size(); i++ )
                    m_channelhead[i] = r;
            }
        }

        return (*m_channelhead[channel_idx])->getChannel( channel_idx );
    }

    //===================================================================================================
    //===================================================================================================

    size_t RecordBuffer::getNumFreeAlloc( const size_t channel_idx )
    {
        size_t num = 0;
        getValidChannel( channel_idx );
        std::list< boost::shared_ptr<Record> >::iterator it = m_channelhead[channel_idx];
        while( it != m_records.end() )
        {
            num += it->get( )->getChannel( channel_idx )->getFree( );
            it ++;
        }
        return num;
    }

    //===================================================================================================
    //===================================================================================================

    void RecordBuffer::flood( )
    {
        while( getNumPartialRecords( ) > 0 )
        {
            m_records.begin( )->get( )->fill( );
            handleRecordFull( );
        }

        for( size_t i=0; i<m_channelhead.size(); i++ )
            m_channelhead[i] =m_records.begin( );
    }

}
