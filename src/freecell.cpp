#include <algorithm>
#include <array>
#include <iostream>
#include <string_view>

#include <unistd.h>
#include <sys/ioctl.h>

namespace csi {

thread_local char csi_buf_[ 100 ];

auto set_alternate_screen() -> std::string_view
{
    return "\033[?1049h";
}

auto reset_alternate_screen() -> std::string_view
{
    return "\033[?1049l";
}

auto reset_cursor( int row = 1, int col = 1 ) -> std::string_view
{
    size_t len = sprintf( csi_buf_, "\033[%d;%dH", row, col );
    return { csi_buf_, len };
}

auto set_fg_color( int color ) -> std::string_view
{
    size_t len = sprintf( csi_buf_, "\033[38;5;%dm", color );
    return { csi_buf_, len };
}

auto set_bg_color( int color ) -> std::string_view
{
    size_t len = sprintf( csi_buf_, "\033[48;5;%dm", color );
    return { csi_buf_, len };
}

auto set_bright() -> std::string_view
{
    return "\033[1m";
}

auto set_no_bright() -> std::string_view
{
    return "\033[22m";
}

} // namespace csi

enum class Suit : uint8_t
{
    None,
    Hearts,
    Diamonds,
    Clubs,
    Spades,
};

std::string_view to_str( const Suit &s )
{
    switch ( s )
    {
    case Suit::None:      return "?";
    case Suit::Hearts:    return u8"♥";
    case Suit::Diamonds:  return u8"♦️";
    case Suit::Clubs:     return u8"♣";
    case Suit::Spades:    return u8"♠️";
    }
};

int get_color( const Suit &s )
{
    switch ( s )
    {
    case Suit::None:      return 0;
    case Suit::Hearts:    return 196;
    case Suit::Diamonds:  return 196;
    case Suit::Clubs:     return 232;
    case Suit::Spades:    return 232;
    }
};

enum class Number : uint8_t
{
    None,
    Ace,
    Two,
    Three,
    Four,
    Five,
    Six,
    Seven,
    Eight,
    Nine,
    Ten,
    Jack,
    Queen,
    King,
};

std::string_view to_str( const Number &n )
{
    switch ( n )
    {
    case Number::None:   return " ?";
    case Number::Ace:    return " 1";
    case Number::Two:    return " 2";
    case Number::Three:  return " 3";
    case Number::Four:   return " 4";
    case Number::Five:   return " 5";
    case Number::Six:    return " 6";
    case Number::Seven:  return " 7";
    case Number::Eight:  return " 8";
    case Number::Nine:   return " 9";
    case Number::Ten:    return "10";
    case Number::Jack:   return " J";
    case Number::Queen:  return " Q";
    case Number::King:   return " K";
    }
};

struct Card
{
    Suit m_suit = Suit::None;
    Number m_number = Number::None;
};

struct Cascade
{
    std::array< Card, 20 > m_cards; // Max number of initial cascade + 12 more cards + null
    int size = 0;
};

std::array< Cascade, 8 > cascades;

std::ostream& operator<<( std::ostream &out, const Card &c )
{
    out << csi::set_bright()
        << csi::set_fg_color( get_color( c.m_suit ) ) << " " << to_str( c.m_number ) << to_str( c.m_suit ) << " "
        << csi::set_no_bright();

    return out;
}

int main()
{
    struct winsize term_size;
    ioctl(STDIN_FILENO, TIOCGWINSZ, &term_size);

    std::cout << csi::set_alternate_screen();
    // Clear screen first
    std::cout << csi::set_bg_color( 232 );
    for ( int i = 0; i < term_size.ws_row * term_size.ws_col; ++i )
        std::cout << " ";

    std::cout << csi::reset_cursor() << "Sleeping for a few seconds...";
    std::cout << csi::reset_cursor( 2, 1 ) << "Term width = " << term_size.ws_col;
    std::cout << csi::reset_cursor( 3, 1 ) << "Term height = " << term_size.ws_row << std::flush;

    std::cout << csi::reset_cursor( 5,1 ) << "Shuffling cards..." << std::flush;
    {
        std::array< Card, 52 > deck;
        for ( uint8_t suit = 1; suit <= 4; ++suit )
        {
            for ( uint8_t number = 1; number <= 13; ++number )
            {
                Card &card = deck[ ( suit - 1 ) * 13 + ( number - 1 ) ];
                card.m_suit = static_cast< Suit >( suit );
                card.m_number = static_cast< Number >( number );
            }
        }

        std::random_shuffle( deck.begin(), deck.end() );

        Cascade *cur_cascade = &cascades[ 0 ];
        for ( const Card &c : deck )
        {
            cur_cascade->m_cards[ cur_cascade->size++ ] = c;
            ++cur_cascade;
            if ( cur_cascade == cascades.end() )
            {
               cur_cascade = &cascades[ 0 ];
            }
        }
    }

    // TODO for testing only, remove
    for ( int i = 0; i < 12; ++i )
    {
        Card c;
        c.m_suit = Suit::Hearts;
        c.m_number = Number::Eight;
        cascades[ 0 ].m_cards[ cascades[ 0 ].size++ ] = c;
    }

    std::cout << csi::reset_cursor( 6,1 ) << "Drawing cards..." << std::flush;

    const int cascade_with = 8;

    const int frame_height = 48;
    const int frame_width = 8 * cascade_with + 3;
    const int frame_start_row = 10;
    const int frame_start_col = ( term_size.ws_col - frame_width ) / 2;

    // Draw frame
    std::cout << csi::set_bg_color( 28 ) << csi::set_fg_color( 255 );
    for ( int row = 0; row < frame_height; ++row )
    {
        std::cout << csi::reset_cursor( frame_start_row + row, frame_start_col );

        std::cout << ( row ==  0 ? u8"┌" :
                       row == frame_height - 1 ? u8"└" : "│" );

        for ( int col = 1; col < frame_width - 1; ++col )
        {
            std::cout << ( ( row ==  0 || row == frame_height - 1 ) ? u8"─" : " " );
        }

        std::cout << ( row ==  0 ? u8"┐" :
                       row == frame_height - 1 ? u8"┘" : "│" );
    }

    auto draw_card = []( const Card &c, int row, int col )
    {
        std::cout << csi::set_bg_color( 255 );

        std::cout << csi::reset_cursor( row, col ) << csi::set_fg_color( 28 ) << u8"▀▀▀▀▀";
        ++row;
        std::cout << csi::reset_cursor( row, col ) << c;
        ++row;

        std::cout << csi::set_fg_color( 28 )
                  << csi::reset_cursor( row,     col ) << u8"     "
                  << csi::reset_cursor( row + 1, col ) << u8"▄▄▄▄▄";

    };

    {
        Card c;
        c.m_suit = Suit::Hearts;
        c.m_number = Number::Eight;

        // Draw cells
        draw_card( c, frame_start_row + 1, frame_start_col + 2 );
        draw_card( c, frame_start_row + 1, frame_start_col + 9 );
        draw_card( c, frame_start_row + 1, frame_start_col + 16 );
        draw_card( c, frame_start_row + 1, frame_start_col + 23 );

        // Draw foundations
        draw_card( c, frame_start_row + 1, frame_start_col + frame_width - 5 - 2 );
        draw_card( c, frame_start_row + 1, frame_start_col + frame_width - 5 - 9 );
        draw_card( c, frame_start_row + 1, frame_start_col + frame_width - 5 - 16 );
        draw_card( c, frame_start_row + 1, frame_start_col + frame_width - 5 - 23 );
    }


    const int top_row = frame_start_row + 6;
    const int start_col = frame_start_col + 3;

    std::cout << csi::set_bg_color( 255 ); // white bg for cards

    for ( size_t c_idx = 0; c_idx < 8; ++c_idx ) // TODO range indexed
    {
        const Cascade &cascade = cascades[ c_idx ];

        int row = top_row;
        int col = start_col + cascade_with * c_idx;

        if ( cascade.m_cards[ 0 ].m_suit == Suit::None )
        {
            std::cout << csi::reset_cursor( row, col ) << csi::set_fg_color( 25 ) << "<...>";
        }
        else
        {
            std::cout << csi::reset_cursor( row, col ) << csi::set_fg_color( 28 ) << u8"▀▀▀▀▀";
            ++row;
            std::cout << csi::reset_cursor( row, col ) << cascade.m_cards[ 0 ];
            ++row;

            for ( size_t card_idx = 1; ; ++card_idx ) // TODO range indexed
            {
                const Card &card = cascade.m_cards[ card_idx ];
                if ( card.m_suit == Suit::None )
                {
                    std::cout << csi::set_fg_color( 28 )
                              << csi::reset_cursor( row,     col ) << u8"     "
                              << csi::reset_cursor( row + 1, col ) << u8"▄▄▄▄▄";
                    break;
                }
                else
                {
                    std::cout << csi::reset_cursor( row, col ) << csi::set_fg_color( 248 ) << u8"─────";
                    ++row;
                    std::cout << csi::reset_cursor( row, col ) << card;
                    ++row;
                }
            }
        }
    }

    std::cout << std::flush;

    sleep( 100 );
    std::cout << csi::reset_alternate_screen();

    return 0;
}
