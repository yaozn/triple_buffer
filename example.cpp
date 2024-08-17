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