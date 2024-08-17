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

#ifndef B5DB47E1_2FC8_48AE_9875_6CA6D2B43D26
#define B5DB47E1_2FC8_48AE_9875_6CA6D2B43D26

#include <atomic>
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <type_traits>

#ifdef __DEBUG__
#define _log( fmt_, ... ) std::printf( "%d: " fmt_, __LINE__, ##__VA_ARGS__ )
#else
#define _log( ... )
#endif

namespace yy {

#define YIELD() std::this_thread::yield()

using block_ptr = char*;

template <size_t SZ>
struct default_block_allocator {
    static block_ptr alloc( int slot_ ) {
        return static_cast<block_ptr>( std::malloc( SZ ) );
    }

    static void free( block_ptr block_, int slot_ ) { std::free( block_ ); }
};

constexpr uint32_t k_write_mask = 0xc0;
constexpr uint32_t k_read_mask  = 0x0c;
constexpr uint32_t k_dirty_mask = 0x100;
constexpr uint32_t k_init_mask  = 0x1b;

template <size_t BLK_SZ, template <size_t> class block_allocator = default_block_allocator>
struct triple_buffer_basic {
    triple_buffer_basic() {
        for ( int i = 0; i < 3; i++ ) {
            _data[ i ] = block_allocator<BLK_SZ>::alloc( i );
        }
    }

    ~triple_buffer_basic() {
        for ( int i = 0; i < 3; i++ ) {
            block_allocator<BLK_SZ>::free( _data[ i ], i );
        }
    }

    triple_buffer_basic( const triple_buffer_basic& )             = delete;
    triple_buffer_basic( const triple_buffer_basic&& )            = delete;
    triple_buffer_basic& operator=( const triple_buffer_basic& )  = delete;
    triple_buffer_basic& operator=( const triple_buffer_basic&& ) = delete;

    block_ptr write_buffer() const {
        auto slot = _flags.load( std::memory_order_consume ) & k_write_mask;
        return _data[ slot >> 6 ];
    }

    bool update( std::function<bool( block_ptr block, size_t limit_len_ )> update_func_ ) {
        if ( update_func_( write_buffer(), BLK_SZ ) ) {
            update();
            return true;
        }

        return false;
    }

    void update( const void* data_, size_t len_ ) {
        assert( len_ <= BLK_SZ );
        memcpy( write_buffer(), data_, std::min( len_, BLK_SZ ) );
        update();
    }

    bool is_update() {
        return !!( _flags.load( std::memory_order_consume ) & k_dirty_mask );
    }

    bool read( std::function<void( block_ptr block, size_t limit_len_ )> read_func_ ) {
        auto data = read_buffer();
        if ( !data ) {
            return false;
        }

        read_func_( data, BLK_SZ );
        return true;
    }

    block_ptr read_buffer() {
        if ( !is_update() ) {
            return nullptr;
        }

        _log( "read flags=%x\n", _flags.load( std::memory_order_consume ) );
        swap_buffer( retained_to_read );
        _log( "after read flags=%x\n", _flags.load( std::memory_order_consume ) );

        auto slot = _flags.load( std::memory_order_consume ) & k_read_mask;
        return _data[ slot >> 2 ];
    }

protected:
    void update() {
        swap_buffer( write_to_retained );
    }

private:
    static uint32_t write_to_retained( uint32_t flags_ ) {
        return ( k_dirty_mask | ( ( flags_ & k_write_mask ) >> 2 ) | ( ( flags_ & 0x30 ) << 2 ) | ( flags_ & k_read_mask ) | ( flags_ & 0x03 ) );
    }

    static uint32_t retained_to_read( uint32_t flags_ ) {
        return ( ( flags_ & k_write_mask ) | ( ( flags_ & 0x30 ) >> 2 ) | ( ( flags_ & k_read_mask ) << 2 ) | ( flags_ & 0x03 ) );
    }

    void swap_buffer( std::function<uint32_t( uint32_t )> exchanger_ ) {
        auto current = _flags.load( std::memory_order_consume );
        _log( "current: %x, to %x\n", current, exchanger_( current ) );
        while ( !_flags.compare_exchange_weak( current, exchanger_( current ), std::memory_order_release, std::memory_order_consume ) )
            YIELD();

        _log( "swapped: %x\n", _flags.load( std::memory_order_consume ) );
    }

protected:
    block_ptr _data[ 3 ];

private:
    std::atomic_uint _flags = k_init_mask;
};

template <typename T, bool = std::is_pod<T>::value, bool = std::is_assignable<T&, const T&>::value>
struct assigner;

template <typename T>
struct assigner<T, false, false> {
    static void assign( T& dst_, const T& src_ ) {
        static_assert( false, "!!not pod and not assignable" );
    }
};

template <typename T>
struct assigner<T, std::is_pod<T>::value, true> {
    static void assign( T& dst_, const T& src_ ) {
        _log( "pod and assignable\n" );
        dst_ = src_;
    }
};

template <typename T>
struct assigner<T, true, false> {
    static void assign( T& dst_, const T& src_ ) {
        _log( "pod and not assignable\n" );
        memcpy( &dst_, &src_, sizeof( T ) );
    }
};

template <typename T, bool = std::is_default_constructible_v<T>>
struct triple_buffer;

template <typename T>
struct triple_buffer<T, false> : triple_buffer_basic<sizeof( T )> {
    static_assert( false, "!!not default constructible" );
};

template <typename T>
struct triple_buffer<T, true> : triple_buffer_basic<sizeof( T )> {
    using object_assigner = assigner<T>;
    using base            = triple_buffer_basic<sizeof( T )>;

    triple_buffer() {
        for ( int i = 0; i < 3; i++ ) {
            new ( base::_data[ i ] ) T();
        }
    }

    bool fetch( T& object_ ) {
        if ( !base::is_update() ) {
            return false;
        }

        object_assigner::assign( object_, *( T* )( base::read_buffer() ) );
        return true;
    }

    void put( const T& object_ ) {
        object_assigner::assign( *( T* )( base::write_buffer() ), object_ );
        base::update();
    }
};
}  // namespace yy

#endif /* B5DB47E1_2FC8_48AE_9875_6CA6D2B43D26 */
