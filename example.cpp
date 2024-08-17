/*-------------------------------------------------------------------------
BSD 3-Clause License

Copyright (c) 2024, YaoZinan

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

wechat: 371536435  email: zinan@outlook.com
-------------------------------------------------------------------------*/

#include <thread>

#include "triplebuffer.hpp"

struct net_packet {
    int type;
    int seq_no;
    int data;
};

struct bad_object {
    virtual ~bad_object() {}                              // non pod
    bad_object& operator=( const bad_object& ) = delete;  // non assignable
};

struct non_default_constructible {
    non_default_constructible( int x_ )
        : x( x_ ) {}
    int x;
};

int main() {

#if 0  // test only pod and default constructible object can be used
    //! case1-will compile error
    yy::triple_buffer<bad_object>                err_buff_1;
    yy::triple_buffer<non_default_constructible> err_buff_2;

#endif

    //! case2-raw data manipulation
    yy::triple_buffer_basic<sizeof( net_packet )> buff;

    net_packet pkt;
    pkt.type   = 1;
    pkt.seq_no = 2;

    buff.update( &pkt, sizeof pkt );

    //! read the data enter the buffer
    assert( buff.is_update() );
    auto ptr = buff.read_buffer();

    assert( ptr != nullptr );

    assert( ( ( net_packet* )ptr )->type == 1 );
    assert( ( ( net_packet* )ptr )->seq_no == 2 );
    //! ...will set false after read
    assert( !buff.is_update() );

    //! use lambda function to store data
    pkt.type   = 2;
    pkt.seq_no = 3;

    //! lambda return false will discard the data
    buff.update( [ & ]( yy::block_ptr ptr_, size_t limit_len_ ) {
        return false;
    } );
    assert( !buff.is_update() );

    //! lambda return true will store the data
    buff.update( [ & ]( yy::block_ptr unit_, size_t limit_len_ ) {
        memcpy( unit_, &pkt, sizeof pkt );
        return true;
    } );

    //! confirm the data is stored
    ptr = buff.read_buffer();
    assert( ptr != nullptr );
    assert( ( ( net_packet* )ptr )->type == 2 );
    assert( ( ( net_packet* )ptr )->seq_no == 3 );
    assert( !buff.is_update() );

    // case3-obejct manipulation
    //! emtpy buffer
    yy::triple_buffer<net_packet> buff2;
    net_packet                    pkt2;

    assert( !buff2.fetch( pkt2 ) );

    //! put pkt to buffer and fetch it to pkt2
    buff2.put( pkt );
    assert( buff2.fetch( pkt2 ) );
    assert( pkt2.type == 2 );
    assert( pkt2.seq_no == 3 );
    //---
    pkt.type   = 3;
    pkt.seq_no = 4;
    buff2.put( pkt );
    assert( buff2.is_update() );  // set dirty

    //! pkt with type=3 will be discarded
    pkt.type   = 4;
    pkt.seq_no = 5;
    buff2.put( pkt );
    assert( buff2.is_update() );
    assert( buff2.fetch( pkt2 ) );
    assert( pkt2.type == 4 );
    assert( pkt2.seq_no == 5 );
    assert( !buff2.is_update() );  // clean

    return 0;
}